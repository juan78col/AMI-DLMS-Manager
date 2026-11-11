#include "device_connector.h"
#include "Server.h"

#include <spdlog/spdlog.h>

#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <vector>

// =========================================================================
// Constructor (sin cambios salvo std::move)
// =========================================================================
DeviceConnector::DeviceConnector(boost::asio::io_context& io,
    Server& srv,
    AsyncPostgresConnector dbConnector)
    : io_(io)
    , server_ref_(srv)
    , db_connector_(std::move(dbConnector))
{}

struct DispositivoInfo {
    std::string ip;
    uint16_t    port;
};

// =========================================================================
// do_check_and_connect_async
// =========================================================================
boost::asio::awaitable<void> DeviceConnector::do_check_and_connect_async()
{
    // =====================================================================
    // FASE 1: query de endpoints — el lease vive SOLO aquí
    // =====================================================================
    std::vector<DispositivoInfo> endpoints_list;

    try {
        auto db = co_await db_connector_();     // ← lease del pool
        if (!db) {
            spdlog::error("[DeviceConnector] DB connector returned null");
            co_return;
        }

        const std::string sql =
            "SELECT ip, port FROM outbound_endpoints WHERE enabled = true";

        ResultSet rs = co_await db->execute_query(sql, {});

        endpoints_list.reserve(rs.countRows());
        for (size_t r = 0; r < rs.countRows(); ++r) {
            std::string ip = rs.get(r, 0);
            std::string portStr = rs.get(r, 1);
            if (ip.empty() || portStr.empty()) continue;

            try {
                uint16_t port = static_cast<uint16_t>(std::stoi(portStr));
                endpoints_list.push_back({ std::move(ip), port });
            }
            catch (const std::exception& e) {
                spdlog::warn("[DeviceConnector] invalid port '{}' for {}: {}",
                    portStr, ip, e.what());
            }
        }
    }
    catch (const std::exception& e) {
        spdlog::error("[DeviceConnector] DB error: {}", e.what());
        co_return;
    }
    // ← AQUÍ el lease se libera (db sale de scope).
    //   A partir de aquí NO retenemos conexión del pool.

    if (endpoints_list.empty()) {
        spdlog::debug("[DeviceConnector] no outbound endpoints to process");
        co_return;
    }

    spdlog::info("[DeviceConnector] processing {} endpoint(s)",
        endpoints_list.size());

    // =====================================================================
    // FASE 2: trabajo de red — sin lease de BD
    // =====================================================================
    for (const auto& ep : endpoints_list) {
        try {
            boost::asio::ip::tcp::resolver resolver(io_);
            auto endpoints = co_await resolver.async_resolve(
                ep.ip, std::to_string(ep.port),
                boost::asio::use_awaitable);

            const int max_attempts = 3;
            bool connected = false;

            for (int attempt = 1; attempt <= max_attempts && !connected; ++attempt) {
                try {
                    auto socket =
                        std::make_shared<boost::asio::ip::tcp::socket>(io_);

                    co_await boost::asio::async_connect(
                        *socket, endpoints, boost::asio::use_awaitable);

                    server_ref_.add_outgoing_connection(socket);
                    connected = true;

                    spdlog::info("[DeviceConnector] connected to {}:{} "
                        "(attempt {}/{})",
                        ep.ip, ep.port, attempt, max_attempts);
                }
                catch (const std::exception& e) {
                    spdlog::warn("[DeviceConnector] connect {}:{} attempt "
                        "{}/{} failed: {}",
                        ep.ip, ep.port, attempt, max_attempts,
                        e.what());
                   
                }
                if (attempt < max_attempts) {
                    // Backoff real: timer local + co_await
                    boost::asio::steady_timer backoff(io_);
                    backoff.expires_after(std::chrono::milliseconds(500));
                    co_await backoff.async_wait(
                        boost::asio::use_awaitable);
                }
            }
        }
        catch (const std::exception& e) {
            spdlog::error("[DeviceConnector] resolve {}:{} failed: {}",
                ep.ip, ep.port, e.what());
        }
    }

    co_return;
}

// =========================================================================
// check_and_connect — sin cambios
// =========================================================================
void DeviceConnector::check_and_connect()
{
    boost::asio::co_spawn(io_, do_check_and_connect_async(),
        boost::asio::detached);
}