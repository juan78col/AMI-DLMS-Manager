#pragma once
#include <boost/asio.hpp>
#include "AsyncDatabase.h"
#include <libpq-fe.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <iostream>

//using namespace std;
//using namespace boost;
//namespace asio = boost::asio;

// ===================================================
// Clase AsyncPostgres — Cliente PostgreSQL asíncrono
// ===================================================
class AsyncPostgresql : public AsyncDatabase {
    PGconn* conn_ = nullptr;
    boost::asio::io_context& io_;
    boost::asio::posix::stream_descriptor socket_;
   
public:
    AsyncPostgresql(boost::asio::io_context& io)
        : io_(io), socket_(io) {
    }

    ~AsyncPostgresql() { close(); }

    // ---- Conexión ----
    boost::asio::awaitable<void> connect(const std::string& conninfo) override {
        conn_ = PQconnectStart(conninfo.c_str());
        if (!conn_) throw std::runtime_error("PQconnectStart failed");
		try {
            int fd = PQsocket(conn_);
            if (fd < 0) throw std::runtime_error("Invalid PostgreSQL socket");
            socket_.assign(fd);

            while (true) {
                PostgresPollingStatusType status = PQconnectPoll(conn_);
                if (status == PGRES_POLLING_READING || status == PGRES_POLLING_WRITING) {
					int current_fd = PQsocket(conn_);
					if (current_fd < 0) throw std::runtime_error("Invalid PostgreSQL socket during polling");
					if (current_fd != socket_.native_handle()) {
						socket_.release();
						socket_.assign(current_fd);
					}
					auto wait_type = (status == PGRES_POLLING_READING) ? boost::asio::posix::stream_descriptor::wait_read
						: boost::asio::posix::stream_descriptor::wait_write;
					co_await socket_.async_wait(wait_type, boost::asio::use_awaitable);
                    
                }
                else if (status == PGRES_POLLING_OK) {
                    break;
                }
                else {
                    throw std::runtime_error(std::string("Connection failed: ") + PQerrorMessage(conn_));
                }
            }
		}
		catch (...) {
            socket_.release();
			PQfinish(conn_);
			conn_ = nullptr;
			throw; // Re-throw para que el llamador maneje la excepción
		}

        
    }

    bool connected() const override {
        return conn_ && PQstatus(conn_) == CONNECTION_OK;
    }

    // ---- Consulta parametrizada ----
    asio::awaitable<ResultSet> execute_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        std::vector<const char*> values;
		values.reserve(params.size());
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
		
        try {
			co_await wait_for_result();
            
            PGresult* res = PQgetResult(conn_);
            if (!res) throw std::runtime_error("Empty result");

            ExecStatusType status = PQresultStatus(res);
            if (status != PGRES_TUPLES_OK) {
				std::string errc = PQerrorMessage(conn_);
				PQclear(res);
				clear_extra_results();
				throw std::runtime_error("Postgres query failed: " + errc);
            }
            ResultSet data = parse_result(res);
            PQclear(res);
            clear_extra_results();
			co_return data;
		}
		catch (...) {
			clear_extra_results();
			throw; // Re-throw para que el llamador maneje la excepción
		}
    }
    // ---- Consultas sin resultado (INSERT, UPDATE, DELETE) ----
    asio::awaitable<std::size_t> execute_non_query(const std::string& sql, const std::vector<std::string>& params = {}) override {
        std::vector<const char*> values;
		values.reserve(params.size());
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

        try {
            co_await wait_for_result();
            PGresult* res = PQgetResult(conn_);
            if (!res) throw std::runtime_error("Empty result");
            ExecStatusType status = PQresultStatus(res);
            bool success = (status == PGRES_COMMAND_OK|| status == PGRES_TUPLES_OK);
            std::size_t affected = 0;
            if (success) {
				const char* tuples = PQcmdTuples(res);
				if (tuples && *tuples) {
					affected = std::strtoul(tuples,nullptr,10);
				}
            }
			else if (status == PGRES_TUPLES_OK) {
				affected = static_cast<std::size_t>(PQntuples(res));
			}
            else {
                std::cerr << "Postgres command error: " << PQerrorMessage(conn_) << std::endl;
            }

            PQclear(res);
            clear_extra_results();
            if (!success) {
                throw std::runtime_error("Postgres command failed");
            }
            co_return affected;
        }
        catch (...) {
			clear_extra_results();
            throw; // Re-throw para que el llamador maneje la excepción
        }
    }
    // ---- Cerrar conexión ----
    void close() override {
        if (conn_) {
			socket_.cancel();
			socket_.release();
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

private:
    // Espera hasta que PostgreSQL esté listo
    asio::awaitable<void> wait_for_result() {
        // 1) Asegurar que la query/params salieron completos por el socket
        // antes de esperar la respuesta (importante con params grandes).
        int flush_status = PQflush(conn_);
        while (flush_status == 1) {
            co_await socket_.async_wait(boost::asio::posix::stream_descriptor::wait_write,
                boost::asio::use_awaitable);
            flush_status = PQflush(conn_);
        }
        if (flush_status < 0) {
            throw std::runtime_error(std::string("PQflush failed: ") + PQerrorMessage(conn_));
        }

        while (PQisBusy(conn_)) {
            co_await socket_.async_wait(boost::asio::posix::stream_descriptor::wait_read,
                boost::asio::use_awaitable);
            if (!PQconsumeInput(conn_))
                throw std::runtime_error(PQerrorMessage(conn_));

        }
        co_return;
    };

    // Limpia resultados adicionales (por ejemplo, notificaciones)
    void clear_extra_results() {
        while (auto* extra = PQgetResult(conn_)) {
            PQclear(extra);
        }
    };

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
