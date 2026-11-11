#pragma once

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "AsyncDatabase.h"
#include "AsyncPostgresql.h"

namespace asio = boost::asio;

// Pool de conexiones PostgreSQL para Asio.
//  - acquire() espera (hasta acquire_timeout) si no hay conexión libre.
//  - Lease RAII: la conexión vuelve sola al destruirse el shared_ptr.
//  - Reconexión automática con backoff (1s -> 30s) y health check (SELECT 1).
//
// Reglas: io_context con UN solo hilo run(); el pool debe vivir más que sus
// leases; llamar a shutdown() antes de destruir el io_context.
class AsyncPostgresPool {
public:
    using seconds = std::chrono::seconds;
    using Lease   = std::shared_ptr<AsyncDatabase>;

    // health_interval = 0 desactiva el health check.
    AsyncPostgresPool(asio::io_context& io, std::string conninfo,
                      std::size_t size = 5,
                      seconds acquire_timeout = seconds(2),
                      seconds health_interval = seconds(30))
        : io_(io), conninfo_(std::move(conninfo)), size_(size),
          acquire_timeout_(acquire_timeout),
          health_interval_(health_interval), health_timer_(io)
    {
        if (size_ == 0) throw std::invalid_argument("pool size must be > 0");
    }

    AsyncPostgresPool(const AsyncPostgresPool&) = delete;
    AsyncPostgresPool& operator=(const AsyncPostgresPool&) = delete;
    ~AsyncPostgresPool() { shutdown(); }

    // La primera conexión debe funcionar (falla rápido y lanza); las demás
    // que fallen se reintentan solas. Arranca también el health check.
    asio::awaitable<void> initialize()
    {
        for (std::size_t i = 0; i < size_; ++i) {
            try {
                give(co_await open_one());
            } catch (const std::exception& e) {
                spdlog::error("Pool: conexión {} falló: {}", i, e.what());
                if (i == 0) throw;
                reconnect();
            }
        }
        if (health_interval_ > seconds::zero()) {
            asio::co_spawn(io_, health_loop(), asio::detached);
        }
    }

    // auto db = co_await pool.acquire();   // se devuelve sola al salir de scope
    asio::awaitable<Lease> acquire()
    {
        if (stopping_) throw std::runtime_error("Pool is shutting down");

        if (auto c = take()) co_return lease(std::move(c));

        auto w = std::make_shared<Waiter>(io_);
        w->timer.expires_after(acquire_timeout_);
        waiters_.push_back(w);

        // Despierta por timeout o porque give() le entregó una conexión.
        co_await w->timer.async_wait(asio::as_tuple(asio::use_awaitable));

        if (w->conn) co_return lease(std::move(w->conn));

        std::erase(waiters_, w);
        throw std::runtime_error(
            stopping_ ? "Pool is shutting down"
                      : "Timeout waiting for a free PostgreSQL connection");
    }

    void shutdown()
    {
        if (stopping_.exchange(true)) return;

        health_timer_.cancel();
        for (auto& t : timers_) if (auto s = t.lock()) s->cancel();
        for (auto& w : waiters_) w->timer.cancel();
        for (auto& c : idle_) c->close();

        timers_.clear();
        waiters_.clear();
        idle_.clear();      // las prestadas se cierran solas al devolverse
    }

private:
    using ConnPtr = std::shared_ptr<AsyncPostgresql>;

    struct Waiter {
        explicit Waiter(asio::io_context& io) : timer(io) {}
        asio::steady_timer timer;
        ConnPtr conn;
    };

    asio::awaitable<ConnPtr> open_one()
    {
        auto c = std::make_shared<AsyncPostgresql>(io_);
        co_await c->connect(conninfo_);
        co_return c;
    }

    // Siguiente conexión libre y sana (FIFO: reparte el uso entre todas).
    ConnPtr take()
    {
        while (!idle_.empty()) {
            auto c = std::move(idle_.front());
            idle_.pop_front();
            if (c->connected()) return c;
            drop(std::move(c));
        }
        return nullptr;
    }

    // Una conexión rota sale de circulación SOLO aquí y lanza UNA reconexión.
    void drop(ConnPtr c)
    {
        c->close();
        reconnect();
    }

    // Primero a quien espera; si nadie, a las libres.
    void give(ConnPtr c)
    {
        if (stopping_) { c->close(); return; }

        if (!waiters_.empty()) {
            auto w = std::move(waiters_.front());
            waiters_.pop_front();
            w->conn = std::move(c);
            w->timer.cancel();
            return;
        }
        idle_.push_back(std::move(c));
    }

    void release(ConnPtr c)
    {
        if (stopping_)            c->close();
        else if (!c->connected()) drop(std::move(c));
        else                      give(std::move(c));
    }

    Lease lease(ConnPtr c)
    {
        AsyncDatabase* raw = c.get();
        return Lease(raw, [this, c = std::move(c)](AsyncDatabase*) mutable {
            if (stopping_) { c->close(); return; }
            // El lease puede morir en otro hilo: volver al hilo del pool.
            asio::post(io_, [this, c = std::move(c)]() mutable {
                release(std::move(c));
            });
        });
    }

    void reconnect()
    {
        if (!stopping_) asio::co_spawn(io_, reconnect_slot(), asio::detached);
    }

    asio::awaitable<void> reconnect_slot()
    {
        auto timer = std::make_shared<asio::steady_timer>(io_);
        std::erase_if(timers_, [](const auto& w) { return w.expired(); });
        timers_.push_back(timer);

        seconds delay(1);
        for (int attempt = 1; !stopping_; ++attempt) {
            try {
                give(co_await open_one());
                spdlog::info("Pool: conexión restablecida (intentos={})", attempt);
                co_return;
            } catch (const std::exception& e) {
                if (attempt == 1 || attempt % 10 == 0) {   // log sin ruido
                    spdlog::warn("Pool: reconexión falló ({}); intento {}, "
                                 "próximo en {}s", e.what(), attempt, delay.count());
                }
            }

            timer->expires_after(delay);
            auto [ec] = co_await timer->async_wait(
                asio::as_tuple(asio::use_awaitable));
            if (ec) co_return;                      // cancelado por shutdown()
            delay = std::min(delay * 2, seconds(30));
        }
    }

    // Revisa las conexiones libres de a una. Si alguien espera mientras una
    // está en chequeo, release() se la entrega al terminar (milisegundos).
    asio::awaitable<void> health_loop()
    {
        while (!stopping_) {
            health_timer_.expires_after(health_interval_);
            auto [ec] = co_await health_timer_.async_wait(
                asio::as_tuple(asio::use_awaitable));
            if (ec || stopping_) co_return;

            for (auto n = idle_.size(); n > 0 && !stopping_; --n) {
                auto c = take();
                if (!c) break;

                try {
                    co_await c->execute_query("SELECT 1");
                } catch (const std::exception& e) {
                    spdlog::warn("Pool: health check falló: {}", e.what());
                }
                release(std::move(c));              // si murió, reconecta
            }
        }
    }

    asio::io_context&   io_;
    std::string         conninfo_;
    std::size_t         size_;
    seconds             acquire_timeout_;
    seconds             health_interval_;
    std::atomic<bool>   stopping_{false};

    std::deque<ConnPtr>                    idle_;
    std::deque<std::shared_ptr<Waiter>>    waiters_;
    asio::steady_timer                     health_timer_;
    std::vector<std::weak_ptr<asio::steady_timer>> timers_;
};
