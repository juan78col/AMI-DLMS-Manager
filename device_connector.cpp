#include "device_connector.h"
#include "Server.h"
#include <iostream>
#include <boost/asio/steady_timer.hpp>
//#include <chrono>
//#include <bits/chrono.h>
namespace asio = boost::asio;
//using namespace std::chrono_literals;

DeviceConnector::DeviceConnector(boost::asio::io_context& io, Server& srv, AsyncPostgresConnector dbConnector)
    : io_(io), server_ref_(srv), db_connector_(dbConnector)
{
}

// Example struct to parse result rows
struct DispositivoInfo {
    std::string ip;
    uint16_t port;
};

asio::awaitable<void> DeviceConnector::do_check_and_connect_async()
{
    try {
        // Obtain db connection
        std::shared_ptr<AsyncDatabase> db = db_connector_();
        if (!db) {
            std::cerr << "DB connector returned null" << std::endl;
            co_return;
        }

        // Example SQL: change to your table
        std::string sql = "SELECT ip, port FROM outbound_endpoints WHERE enabled = true";

        ResultSet rs = co_await db->execute_query(sql);

        for (size_t r = 0; r < rs.countRows(); ++r) {
            std::string ip = rs.get(r, 0);
            std::string portStr = rs.get(r, 1);
            if (ip.empty() || portStr.empty()) continue;
            uint16_t port = static_cast<uint16_t>(std::stoi(portStr));

            // resolve async
            try {
                asio::ip::tcp::resolver resolver(io_);
                auto endpoints = co_await resolver.async_resolve(ip, std::to_string(port), asio::use_awaitable);

                // try connecting with a simple retry/backoff
                const int max_attempts = 3;
                int attempt = 0;
                bool connected = false;
                while (attempt < max_attempts && !connected) {
                    ++attempt;
                    try {
                        auto socket = std::make_shared<tcp::socket>(io_);
                        co_await asio::async_connect(*socket, endpoints, asio::use_awaitable);
                        // register with server
                        server_ref_.add_outgoing_connection(socket);
                        connected = true;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Connect attempt " << attempt << " to " << ip << ":" << port << " failed: " << e.what() << std::endl;
                        if (attempt < max_attempts) {
                            // simple backoff
                            using namespace std::chrono_literals;
                            /*co_await*/ boost::asio::steady_timer(io_, 500ms).async_wait(asio::use_awaitable);
                        }
                    }
                }

            }
            catch (const std::exception& e) {
                std::cerr << "Resolve failed for " << ip << ":" << port << " - " << e.what() << std::endl;
            }
        }

    }
    catch (const std::exception& e) {
        std::cerr << "DeviceConnector DB error: " << e.what() << std::endl;
    }

    co_return;
}

void DeviceConnector::check_and_connect()
{
    // spawn coroutine
    asio::co_spawn(io_, do_check_and_connect_async(), asio::detached);
}