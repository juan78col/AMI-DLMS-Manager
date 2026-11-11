#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <memory>

#include "Server.h"
#include "AsyncPostgresqlConnector.h"   // ← sin "ql"

class DeviceConnector {
public:
    DeviceConnector(boost::asio::io_context& io,
        Server& srv,
        AsyncPostgresConnector dbConnector);

    void check_and_connect();

private:
    boost::asio::io_context& io_;
    Server& server_ref_;
    AsyncPostgresConnector   db_connector_;

    boost::asio::awaitable<void> do_check_and_connect_async();
};