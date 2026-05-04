#include "Server.h"
// Server.cpp
#include "Session.h" // Se incluye aquí porque necesitamos crear un std::make_shared<Session>.

// Implementación del constructor.
Server::Server(asio::io_context& io_context, unsigned short port, /*DbConnector*/ AsyncDatabaseConnector connector)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
    connector_(connector),
    cleanup_timer_(io_context),
    client_counter(0) // Es buena práctica inicializar todos los miembros.
{//std::move(connector)

    // Inicia el ciclo para aceptar conexiones.
    do_accept();
    cleanup_sessions();
}

// New add_outgoing_connection: register session into maps and apply deduplication logic
void Server::add_outgoing_connection(std::shared_ptr<tcp::socket> socket)
{
    try {
        auto remote_ip = socket->remote_endpoint().address().to_string();
        auto remote_port = socket->remote_endpoint().port();
        std::string endpoint_key = remote_ip + ":" + std::to_string(remote_port);

        // Create session and apply same deduplication as in incoming accept
        auto session = std::make_shared<Session>(std::move(*socket), remote_ip, remote_port, connector_);

        // Check duplicates by endpoint
        if (auto existing = sessions_by_endpoint_[endpoint_key].lock()) {
            std::cout << "Sesion duplicada detectada (outgoing): " << endpoint_key << " cerrando anterior." << std::endl;
            existing->Close();
        }

        // Register session in endpoint map
        sessions_by_endpoint_[endpoint_key] = session;

        // If session identifies a meter later, it will call set_on_device_identified and be added to sessions_by_meter_
        session->set_on_device_identified(
            [this, weak_sess = std::weak_ptr<Session>(session)](const std::string& meter_id) {
                if (auto new_session = weak_sess.lock()) {
                    // If there is an existing session for this meter, close it
                    auto it = sessions_by_meter_.find(meter_id);
                    if (it != sessions_by_meter_.end()) {
                        if (auto old_session = it->second.lock()) {
                            std::cout << "Reemplazando sesion anterior del medidor " << meter_id << "\n";
                            old_session->Close();
                        }
                    }
                    sessions_by_meter_[meter_id] = new_session;
                    std::cout << "Nueva sesion registrada para medidor " << meter_id << std::endl;
                }
            });

        // Start the session
        session->start();

    }
    catch (const std::exception& ex) {
        std::cerr << "Failed to add outgoing connection: " << ex.what() << std::endl;
    }
}

// Implementación del método para aceptar conexiones.
void Server::do_accept() {
    acceptor_.async_accept(
        [this](std::error_code ec, boost::asio::ip::tcp::socket socket) {
            if (!ec) {
                client_counter++;
                auto remote_ip = socket.remote_endpoint().address().to_string();
                auto remote_port = socket.remote_endpoint().port();
                std::string endpoint_key = remote_ip + ":" + std::to_string(remote_port);

                //Verificar si ya existe una sesión con la misma IP:puerto
                if (auto existing = sessions_by_endpoint_[endpoint_key].lock()) {
                    std::cout << "Sesion duplicada detectada: " << endpoint_key << "cerrando anterior." << std::endl;
                    existing->Close();
                }

                std::cout << "Nueva conexion (" << client_counter << "): "
                    << remote_ip << ":" << remote_port << "\n";

                // Creamos la sesión y la iniciamos.
                auto session = std::make_shared<Session>(
                    std::move(socket), remote_ip, remote_port, connector_);

                //establecemos un callback que se llamara cuando la sesion indetnifique el disppsitivo medidor,dcu,macro,etc
                session->set_on_device_identified(
                    [this, weak_sess = std::weak_ptr<Session>(session)](const std::string& meter_id) {
                        if (auto new_session = weak_sess.lock()) {

                            // Si ya existe una sesión para este medidor, la cerramos
                            auto it = sessions_by_meter_.find(meter_id);
                            if (it != sessions_by_meter_.end()) {
                                if (auto old_session = it->second.lock()) {
                                    std::cout << "Reemplazando sesion anterior del medidor " << meter_id << "\n";
                                    old_session->Close(); //cierre limpio
                                }
                            }

                            // Registrar la nueva sesión
                            sessions_by_meter_[meter_id] = new_session;
                            std::cout << "Nueva sesion registrada para medidor "
                                << meter_id << std::endl;
                        }
                    });

                session->start();

                // Register in endpoint map (ownership is the session itself while active coroutines hold shared_ptrs)
                sessions_by_endpoint_[endpoint_key] = session;

                //sessions_by_meter_[Session::m_concentrator] = session; **REVISAR PENDIENTE***
            }

            // Continuamos aceptando la siguiente conexión, sin importar si la anterior tuvo éxito.
            do_accept();
        });
}

void Server::cleanup_sessions()
{
    cleanup_timer_.expires_after(std::chrono::seconds(30));
    cleanup_timer_.async_wait([this](const boost::system::error_code& ec) {
        if (ec) return;
        
        // Limpiar sesiones TCP inactivas
        for (auto it = sessions_by_endpoint_.begin(); it != sessions_by_endpoint_.end(); ) {
            if (it->second.expired()) {
                it = sessions_by_endpoint_.erase(it);
            }
            else {
                ++it;
            }
        }
        
        //Limpiar sesiones por medidor inactivas
        for (auto it = sessions_by_meter_.begin(); it != sessions_by_meter_.end(); ) {
            if (it->second.expired()) {
                it = sessions_by_meter_.erase(it);
            }
            else {
                ++it;
            }
        }
        
        // Reprogramar limpieza
        cleanup_sessions();
        });

}
