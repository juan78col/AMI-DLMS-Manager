#pragma once
#include "AsyncDatabase.h"
#include <boost/asio.hpp>
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <iostream>

using namespace std;
using namespace boost;
namespace asio = boost::asio;

// ===================================================
// Clase AsyncPostgres — Cliente PostgreSQL asíncrono
// ===================================================
class AsyncPostgresql : public AsyncDatabase {
    PGconn* conn_ = nullptr;
    asio::io_context& io_;
    asio::posix::stream_descriptor socket_;
   
public:
    AsyncPostgresql(asio::io_context& io)
        : io_(io), socket_(io) {
    }

    ~AsyncPostgresql() { close(); }

    // ---- Conexión ----
    asio::awaitable<void> connect(const std::string& conninfo) override {
        conn_ = PQconnectStart(conninfo.c_str());
        if (!conn_)
            throw std::runtime_error("PQconnectStart failed");

        int fd = PQsocket(conn_);
        if (fd < 0)
            throw std::runtime_error("Invalid PostgreSQL socket");
        socket_.assign(fd);

        while (true) {
            PostgresPollingStatusType status = PQconnectPoll(conn_);
            if (status == PGRES_POLLING_READING) {
                co_await socket_.async_wait(asio::posix::stream_descriptor::wait_read, asio::use_awaitable);
            }
            else if (status == PGRES_POLLING_WRITING) {
                co_await socket_.async_wait(asio::posix::stream_descriptor::wait_write, asio::use_awaitable);
            }
            else if (status == PGRES_POLLING_OK) {
                break;
            }
            else {
                throw std::runtime_error(std::string("Connection failed: ") + PQerrorMessage(conn_));
            }
        }
    }

    bool connected() const override {
        return conn_ && PQstatus(conn_) == CONNECTION_OK;
    }

    // ---- Consulta parametrizada ----
    asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        std::vector<const char*> values;
        for (auto& p : params) values.push_back(p.c_str());
        int ok;
        if (params.empty()) {
            ok = PQsendQuery(conn_, sql.c_str());
        }
        else {
            ok = PQsendQueryParams(conn_, sql.c_str(), (int)params.size(), nullptr,
                values.data(), nullptr, nullptr, 0);
        }
        
        
        if (!ok)
            throw std::runtime_error("PQsendQuery failed");

        co_await wait_for_result();
        PGresult* res = PQgetResult(conn_);
        if (!res)
            throw std::runtime_error("Empty result");

        ExecStatusType status = PQresultStatus(res);
        if (status != PGRES_TUPLES_OK)
            throw std::runtime_error(PQerrorMessage(conn_));

        ResultSet data = parse_result(res);
        PQclear(res);
        clear_extra_results();
        co_return data;
    }
    // ---- Consultas sin resultado (INSERT, UPDATE, DELETE) ----
    asio::awaitable<bool> execute_non_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        std::vector<const char*> values;
        for (auto& p : params) values.push_back(p.c_str());

        bool ok;
        if (params.empty()) {
            ok = PQsendQuery(conn_, sql.c_str());
        }
        else {
            ok = PQsendQueryParams(conn_, sql.c_str(), (int)params.size(), nullptr,
                values.data(), nullptr, nullptr, 0);
        }

        if (!ok)
            throw std::runtime_error("PQsendQuery failed");

        co_await wait_for_result();

        PGresult* res = PQgetResult(conn_);
        if (!res)
            throw std::runtime_error("Empty result");

        ExecStatusType status = PQresultStatus(res);
        bool success = (status == PGRES_COMMAND_OK);

        if (!success) {
            std::cerr << "Postgres command error: " << PQerrorMessage(conn_) << std::endl;
        }

        PQclear(res);
        clear_extra_results();
        co_return success;
    }
    // ---- Cerrar conexión ----
    void close() override {
        if (conn_) {
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

private:
    // Espera hasta que PostgreSQL esté listo
    asio::awaitable<void> wait_for_result() {
        while (PQisBusy(conn_)) {
            co_await socket_.async_wait(asio::posix::stream_descriptor::wait_read,
                asio::use_awaitable);
            if (!PQconsumeInput(conn_))
                throw std::runtime_error(PQerrorMessage(conn_));

        }
        co_return;
    }

    // Limpia resultados adicionales (por ejemplo, notificaciones)
    void clear_extra_results() {
        while (auto* extra = PQgetResult(conn_)) {
            PQclear(extra);
        }
    }

    // Convierte PGresult → ResultSet
    ResultSet parse_result(PGresult* res) {
        ResultSet result;
        int nrows = PQntuples(res);
        int ncols = PQnfields(res);
        for (int i = 0; i < nrows; ++i) {
            std::vector<std::string> row;
            row.reserve(ncols);
            for (int j = 0; j < ncols; ++j) {
                char* val = PQgetvalue(res, i, j);
                row.emplace_back(val ? val : "");
            }
            result.addRow(row);
        }
        return result;
    }
};

// ===================================================
// Pool de conexiones opcional (multi-session server)
// ===================================================
class AsyncPostgresPool {
public:
    AsyncPostgresPool(asio::io_context& io, std::string conninfo, size_t size)
        : io_(io), conninfo_(std::move(conninfo)), pool_size_(size) {
        pool_.reserve(size);
    }

    // Inicializa el pool con conexiones válidas
    boost::asio::awaitable<void> initialize() {
        for (size_t i = 0; i < pool_size_; ++i) {
            try {
                auto conn = std::make_shared<AsyncPostgresql>(io_);
                co_await conn->connect(conninfo_);
                pool_.push_back(conn);
            }
            catch (const std::exception& e) {
                std::cerr << "Connection " << i << " failed: " << e.what() << std::endl;
            }
        }

        if (pool_.empty())
            throw std::runtime_error("No connections could be established");
    }

    // Obtiene una conexión disponible (round-robin simple)
    std::shared_ptr<AsyncPostgresql> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty())
            throw std::runtime_error("No available PostgreSQL connections");

        for (size_t i = 0; i < pool_.size(); ++i) {
            auto& conn = pool_[next_++ % pool_.size()];
            if (conn->connected()) {
                return conn;
            }
        }

        throw std::runtime_error("No healthy PostgreSQL connections");
    }

    // Cierra todas las conexiones
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& conn : pool_) {
            conn->close();
        }
        pool_.clear();
    }

private:
    asio::io_context& io_;
    std::string conninfo_;
    size_t pool_size_;
    std::vector<std::shared_ptr<AsyncPostgresql>> pool_;
    std::mutex mutex_;
    size_t next_ = 0;
};