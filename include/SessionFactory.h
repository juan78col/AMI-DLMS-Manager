#pragma once
#include <boost/asio.hpp>
#include "Session.h"
#include "AsyncPostgresqlConnector.h"
#include <functional>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
// New helper: consulta la base de datos usando el conector proporcionado para obtener endpoints (ip, port)
// Crea sesiones salientes para cada fila devuelta y las retorna.
// - dbConnector: función que devuelve una conexión (desde pool) implementando AsyncDatabase
// - sql: consulta que debe devolver al menos dos columnas: ip (text) y port (int)
// - on_new_session: callback opcional que será invocado para cada sesión creada (p. ej. para registrarla en un gestor)
inline asio::awaitable<std::vector<std::shared_ptr<Session>>> create_outbound_sessions_from_db(
    asio::io_context& io,
    AsyncPostgresConnector dbConnector,
    const std::string& sql = "SELECT ip, port FROM outbound_endpoints WHERE enabled = true",
    std::function<void(std::shared_ptr<Session>)> on_new_session = nullptr)
{
    std::vector<std::shared_ptr<Session>> sessions;

    // Obtener conexión a la base de datos desde el conector
    std::shared_ptr<AsyncDatabase> db = dbConnector();
    if (!db) {
        throw std::runtime_error("Database connector returned null");
    }

    // Ejecutar la consulta y obtener resultados
    ResultSet rs = co_await db->execute_query(sql);

    for (size_t r = 0; r < rs.countRows(); ++r) {
        try {
            std::string ip = rs.get(r, 0);
            std::string portStr = rs.get(r, 1);
            if (ip.empty() || portStr.empty()) {
                std::cerr << "Skipping empty endpoint row " << r << std::endl;
                continue;
            }

            unsigned short port = static_cast<unsigned short>(std::stoi(portStr));

            // Resolver y conectar
            tcp::resolver resolver(io);
            auto endpoints = co_await resolver.async_resolve(ip, std::to_string(port), asio::use_awaitable);

            tcp::socket socket(io);
            co_await asio::async_connect(socket, endpoints, asio::use_awaitable);

            auto remote_ip = socket.remote_endpoint().address().to_string();
            auto remote_port = socket.remote_endpoint().port();

            auto session = std::make_shared<Session>(std::move(socket), remote_ip, remote_port, dbConnector);
            session->start();

            // Callback de registro si se proporcionó
            if (on_new_session) on_new_session(session);

            sessions.push_back(session);
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to create outbound session for row " << r << ": " << e.what() << std::endl;
            continue;
        }
    }

    co_return sessions;
}
