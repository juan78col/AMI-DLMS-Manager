#pragma once
#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>

#include <memory>
#include <iostream>

// Incluimos la cabecera de DbConnector porque es un miembro por valor.
//#include "DbConnector.h" //para ozo
#include "AsyncPostgresqlConnector.h"
#include "Session.h"
// Declaración adelantada (Forward Declaration) de la clase Session.
// Le decimos al compilador "confía en mí, esta clase existe en alguna parte".
// Esto evita tener que incluir Session.hpp aquí, reduciendo dependencias
// y acelerando la compilación.
namespace asio = boost::asio;

//class Session;

using asio::ip::tcp;

class Server {
public:
    // Constructor: ahora acepta un conector genérico de AsyncDatabase
    Server(asio::io_context& io_context, unsigned short port, AsyncDatabaseConnector connector);
	void add_outgoing_connection(std::shared_ptr<tcp::socket> socket);
private:
    // Declaración del método que maneja las conexiones entrantes.
    void do_accept();

    void cleanup_sessions();

    // Declaración de las variables miembro.
    tcp::acceptor acceptor_;
    AsyncDatabaseConnector connector_;
    //Timer para limpieza automática
    boost::asio::steady_timer cleanup_timer_;
    int client_counter;
    //Mapa de sesiones por IP:puerto (control TCP)
    std::unordered_map<std::string, std::weak_ptr<Session>> sessions_by_endpoint_;
    //Mapa de sesiones por medidor (una sesión por dispositivo)
    std::unordered_map<std::string, std::weak_ptr<Session>> sessions_by_meter_;
    
};

#endif // SERVER_HPP

