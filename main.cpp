#define BOOST_ASIO_HAS_STD_COROUTINE
#include <boost/asio.hpp>
// #include <boost/asio/steady_timer.hpp>
// #include <asio/steady_timer.hpp>
#include <iostream>
#include <memory>
// gurux
// #include "conexionbase.h"
// #include "mapsconf.h"

// #include "db/Cpostgresql.h"
// Cpostgresql basedatos("dlmsasio");
// std::unordered_map<int, MeterConfig> m_MeterConfigurations;

// using asio::ip::tcp;
// using namespace boost;
namespace asio = boost::asio;
// using boost::asio::ip::tcp;
// using boost::asio::steady_timer;
using namespace std::chrono;

// #define BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT
// #include "DbConnector.h"

#include "Server.h"
#include "AsyncOracle.h"
#include "AsyncDatabase.h"
#include "AsyncPostgresql.h"
#include "device_connector.h"

bool ENABLE_PGBOUNCER = false;

// Generic AsyncDatabasePool that works with any AsyncDatabase implementation
class AsyncDatabasePool {
public:
    using Factory = std::function<std::shared_ptr<AsyncDatabase>(asio::io_context&)>;

    AsyncDatabasePool(asio::io_context& io, Factory factory, std::string conninfo, size_t size)
        : io_(io), factory_(std::move(factory)), conninfo_(std::move(conninfo)), pool_size_(size) {
        pool_.reserve(size);
    }

    // Inicializa el pool con conexiones válidas
    asio::awaitable<void> initialize() {
        for (size_t i = 0; i < pool_size_; ++i) {
            try {
                auto conn = factory_(io_);
                co_await conn->connect(conninfo_);
                pool_.push_back(conn);
            }
            catch (const std::exception& e) {
                std::cerr << "Generic connection " << i << " failed: " << e.what() << std::endl;
            }
        }

        if (pool_.empty())
            throw std::runtime_error("No connections could be established (generic pool)");
    }

    // Obtiene una conexión disponible (round-robin simple)
    std::shared_ptr<AsyncDatabase> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_.empty())
            throw std::runtime_error("No available database connections");

        for (size_t i = 0; i < pool_.size(); ++i) {
            auto& conn = pool_[next_++ % pool_.size()];
            if (conn->connected()) {
                return conn;
            }
        }

        throw std::runtime_error("No healthy database connections");
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
    Factory factory_;
    std::string conninfo_;
    size_t pool_size_;
    std::vector<std::shared_ptr<AsyncDatabase>> pool_;
    std::mutex mutex_;
    size_t next_ = 0;
};

int main()
{
    try
    {
        boost::asio::io_context io_context;
        
        std::string conn_str = "host=10.9.0.1 port=5432 dbname=medicion_avanzada user=principal password=d113B01dB4 application_name=myapp";
        // Crear pool específico de Postgres (código existente conservado)
        AsyncPostgresPool pool(io_context, conn_str, 5);
        asio::co_spawn(io_context, pool.initialize(), asio::detached);

        // Ejemplo de pool genérico (descomentarlo si prefieres usar el pool genérico)
        /*
        AsyncDatabasePool genericPool(io_context,
            [](asio::io_context& io){ return std::make_shared<AsyncPostgresql>(io); },
            conn_str, 5);
        asio::co_spawn(io_context, genericPool.initialize(), asio::detached);

        // Cambia el conector para usar el pool genérico (si lo deseas)
        // AsyncPostgresConnector connector = [&genericPool]() { return genericPool.acquire(); };
        */

        /*** EJEMPLO 2 /
        // Active example: crear un pool genérico usando AsyncPostgresql detrás (misma lógica que el pool de Postgres)
        AsyncDatabasePool genericPoolActive(io_context,
            [](asio::io_context& io){ return std::make_shared<AsyncPostgresql>(io); },
            conn_str, 5);
        asio::co_spawn(io_context, genericPoolActive.initialize(), asio::detached);

        // Conector que devuelve std::shared_ptr<AsyncDatabase> compatible con Server
        AsyncPostgresConnector connectorGeneric = [&genericPoolActive]() { return genericPoolActive.acquire(); };

        // Puedes crear servidores que usen el conector genérico (pueden coexistir con los servidores que usan el pool específico)
        Server genericServer(io_context, 8878, connectorGeneric);
        Server genericServer2(io_context, 4060, connectorGeneric);
        */
        
        // ejemplo de uso de AsyncOracle: conexión única (comentado hasta que instales OCCI)
        /*
        auto single_oracle = std::make_shared<AsyncOracle>(io_context);
        // Cambia user/password/connectString por los valores de tu entorno Oracle
        asio::co_spawn(io_context, single_oracle->connect("myuser:mypassword@//localhost:1521/XEPDB1"), asio::detached);

        // Ejemplo: ejecutar una consulta usando la conexión única
        asio::co_spawn(io_context,
            [single_oracle]() -> asio::awaitable<void> {
                try {
                    auto rs = co_await single_oracle->execute_query("SELECT sysdate FROM dual");
                    if (rs.countRows() > 0 && rs.countFields() > 0) {
                        std::cout << "Oracle single connection query result: " << rs.get(0,0) << std::endl;
                    }
                }
                catch (const std::exception &e) {
                    std::cerr << "Oracle query error: " << e.what() << std::endl;
                }
                co_return;
            }, asio::detached);
        */

        // Simular el mismo patrón que OZO: el “connector” (usa el pool de Postgres existente)
        AsyncPostgresConnector connector = [&pool]()
        {
            return pool.acquire();
        };

        /*
         * para uso de pgbounces quien nos administra las conexiones
         */
        /*
        auto single_connection = std::make_shared<AsyncPostgresql>(io_context);
        asio::co_spawn(io_context, single_connection->connect(conn_str), asio::detached);

        // Si deseas seguir usando el mismo patrón de inyección
        AsyncPostgresConnector connector = [single_connection]() {
            return single_connection;
            };
        */

        // Inyectar en el servidor igual que antes para dcu
        Server server(io_context, 8877, connector);
        Server server2(io_context, 4059, connector); // medidores cliente
        //funciones para conectarse a dispositivos externos en modo servidor
		DeviceConnector device_connector(io_context,server,connector);
		//ejecutar primera carga
        device_connector.check_and_connect();
		//programar ejecución periódica cada X segundos
		asio::steady_timer timer(io_context, std::chrono::seconds(60));
		std::function<void(const boost::system::error_code&)> timer_handler;
		timer_handler = [&](const boost::system::error_code& ec)
			{
				if (!ec)
				{
					device_connector.check_and_connect();
					timer.expires_after(std::chrono::seconds(60)); // cada 1 minutos
					timer.async_wait(timer_handler);
                    
				}
			};
		timer.async_wait(timer_handler);


        io_context.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Excepción: " << e.what() << "\n";
    }
}