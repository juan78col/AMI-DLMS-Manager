#pragma once
#include "AsyncDatabase.h"
#include <boost/asio.hpp>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <functional>
#include <thread>
#include <future>
#include <chrono>

namespace asio = boost::asio;

// If OCCI is available provide a real implementation, otherwise provide a stub so the project compiles.
#if __has_include(<occi.h>)
#include <occi.h>
using namespace std;

class AsyncOracle : public AsyncDatabase {
public:
    AsyncOracle(asio::io_context& io)
        : io_(io), pool_(std::max(1u, std::thread::hardware_concurrency())) {}

    ~AsyncOracle() { close(); }

    // connect accepts a single conninfo string in the form "user:password@connectString"
    asio::awaitable<void> connect(const std::string& conninfo) override {
        auto at = conninfo.find('@');
        if (at == std::string::npos)
            throw std::runtime_error("Invalid Oracle conninfo format, expected user:password@connectString");
        auto up = conninfo.substr(0, at);
        auto cs = conninfo.substr(at + 1);
        auto colon = up.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("Invalid Oracle conninfo format, expected user:password@connectString");
        std::string user = up.substr(0, colon);
        std::string password = up.substr(colon + 1);

        co_await run_blocking([this, user, password, cs]() {
            try {
                env_ = oracle::occi::Environment::createEnvironment(oracle::occi::Environment::DEFAULT);
                conn_ = env_->createConnection(user, password, cs);
            }
            catch (const oracle::occi::SQLException& e) {
                throw std::runtime_error(std::string("OCCI connect error: ") + e.getMessage());
            }
        });
    }

    // Overload for explicit params
    asio::awaitable<void> connect(const std::string& user, const std::string& password, const std::string& connectString) {
        co_await run_blocking([this, user, password, connectString]() {
            try {
                env_ = oracle::occi::Environment::createEnvironment(oracle::occi::Environment::DEFAULT);
                conn_ = env_->createConnection(user, password, connectString);
            }
            catch (const oracle::occi::SQLException& e) {
                throw std::runtime_error(std::string("OCCI connect error: ") + e.getMessage());
            }
        });
    }

    bool connected() const override {
        return conn_ != nullptr;
    }

    asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        ResultSet result;
        co_await run_blocking([this, &sql, &params, &result]() {
            try {
                oracle::occi::Statement* stmt = conn_->createStatement(sql);
                for (size_t i = 0; i < params.size(); ++i) {
                    stmt->setString(static_cast<unsigned int>(i + 1), params[i]);
                }
                oracle::occi::ResultSet* rs = stmt->executeQuery();
                oracle::occi::ResultSetMetaData rmd = rs->getMetaData();
                int ncols = rmd.getColumnCount();
                while (rs->next()) {
                    std::vector<std::string> row;
                    row.reserve(ncols);
                    for (int c = 1; c <= ncols; ++c) {
                        std::string v = rs->getString(c);
                        row.push_back(v);
                    }
                    result.addRow(row);
                }
                stmt->closeResultSet(rs);
                conn_->terminateStatement(stmt);
            }
            catch (const oracle::occi::SQLException& e) {
                throw std::runtime_error(std::string("OCCI query error: ") + e.getMessage());
            }
        });
        co_return result;
    }

    asio::awaitable<bool> execute_non_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        bool success = false;
        co_await run_blocking([this, &sql, &params, &success]() {
            try {
                oracle::occi::Statement* stmt = conn_->createStatement(sql);
                for (size_t i = 0; i < params.size(); ++i) {
                    stmt->setString(static_cast<unsigned int>(i + 1), params[i]);
                }
                int updated = stmt->executeUpdate();
                conn_->terminateStatement(stmt);
                success = (updated >= 0);
            }
            catch (const oracle::occi::SQLException& e) {
                std::cerr << "OCCI non-query error: " << e.getMessage() << std::endl;
                success = false;
            }
        });
        co_return success;
    }

    void close() override {
        if (conn_ || env_) {
            try {
                asio::post(pool_, [this]() {
                    try {
                        if (conn_) {
                            env_->terminateConnection(conn_);
                            conn_ = nullptr;
                        }
                        if (env_) {
                            oracle::occi::Environment::terminateEnvironment(env_);
                            env_ = nullptr;
                        }
                    }
                    catch (...) {}
                });
                pool_.join();
            }
            catch (...) {}
        }
    }

private:
    asio::io_context& io_;
    asio::thread_pool pool_;
    oracle::occi::Environment* env_ = nullptr;
    oracle::occi::Connection* conn_ = nullptr;

    // helper: run a blocking function on the thread_pool and resume coroutine on io_context
    asio::awaitable<void> run_blocking(std::function<void()> func) {
        auto executor = co_await asio::this_coro::executor;
        std::promise<void> p;
        auto f = p.get_future();
        asio::post(pool_, [func = std::move(func), &p]() mutable {
            try {
                func();
                p.set_value();
            }
            catch (...) {
                try {
                    p.set_exception(std::current_exception());
                }
                catch (...) {}
            }
        });

        // wait on future without blocking io thread by polling
        while (f.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            co_await asio::post(executor, asio::use_awaitable);
        }

        // get will rethrow exception if occurred
        f.get();
        co_return;
    }
};

#else

// OCCI not available: provide a stub that preserves API but throws at runtime
class AsyncOracle : public AsyncDatabase {
public:
    AsyncOracle(asio::io_context& io) : io_(io) {}
    ~AsyncOracle() override { close(); }

    asio::awaitable<void> connect(const std::string& conninfo) override {
        throw std::runtime_error("AsyncOracle not available: OCCI headers not found at compile time");
        co_return;
    }

    bool connected() const override { return false; }

    asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        throw std::runtime_error("AsyncOracle not available: OCCI headers not found at compile time");
        co_return ResultSet{};
    }

    asio::awaitable<bool> execute_non_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        throw std::runtime_error("AsyncOracle not available: OCCI headers not found at compile time");
        co_return false;
    }

    void close() override {}

private:
    asio::io_context& io_;
};

#endif
