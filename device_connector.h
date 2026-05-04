#pragma once
#pragma once
#define BOOST_ASIO_HAS_STD_COROUTINE
// Detectar si C++20 está disponible
#if __cplusplus >= 202002L && defined(__cpp_impl_coroutine)
#define HAS_CPP20_COROUTINES 1
#else
#define HAS_CPP20_COROUTINES 0
#endif

#include <boost/asio.hpp>
#include <memory>
#include "Server.h"
#include <chrono>
#include "AsyncPostgresqlConnector.h"

using boost::asio::ip::tcp;

namespace asio = boost::asio;

using tcp = boost::asio::ip::tcp;


class DeviceConnector {
public:
    DeviceConnector(boost::asio::io_context& io, Server& srv, AsyncPostgresConnector dbConnector);

    void check_and_connect();

private:
    boost::asio::io_context& io_;
    Server& server_ref_;
    AsyncPostgresConnector db_connector_;

    // coroutine helper that does the async DB query and connects
    asio::awaitable<void> do_check_and_connect_async();
};