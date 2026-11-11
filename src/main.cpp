#include <boost/asio.hpp>
#include <iostream>
#include <memory>

using namespace std::chrono;
#include "Server.h"
#include "AsyncOracle.h"
#include "AsyncDatabase.h"
#include "AsyncPostgresql.h"
#include "device_connector.h"
#include "SecretManager.h"
#include "dlms_profile_service.h"
#include "logger_init.h"
#include "AsyncPostgresqlPool.h"
bool ENABLE_PGBOUNCER = false;

int main()
{
   try
    {
        InitLogging();
		spdlog::info("DLMS service starting...");
		const char* key = std::getenv("AMI_MASTER_KEY_HEX");
        if (key == nullptr) {
			std::cerr << "Error: AMI_MASTER_KEY_HEX environment variable is not set." << std::endl;
            ShutdownLogging();
			return 1;
        }
		auto secretManager = std::make_shared<const ami::security::SecretManager>(key);

		PluginLoader::instance().load("./plugins");
        		
        boost::asio::io_context io_context;
		const char* host = std::getenv("PG_HOST");
		const char* port = std::getenv("PG_PORT");
		const char* dbname = std::getenv("PG_DBNAME");
		const char* user = std::getenv("PG_USER");
		const char* password = std::getenv("PG_PASSWORD");
		const char* application_name = std::getenv("PG_APPLICATION_NAME"); 
		std::string bootstrapError="";

		if (!host || !port || !dbname || !user || !password || !application_name) {
			std::cerr << "Error: One or more PostgreSQL environment variables are not set."
				<< " Please set PG_HOST, PG_PORT, PG_DBNAME, PG_USER, PG_PASSWORD, and PG_APPLICATION_NAME." << std::endl;
			ShutdownLogging();
			return 1;
        }
		std::string conn_str = "host=" + std::string(host) + " port=" + std::string(port) +
			" dbname=" + std::string(dbname) + " user=" + std::string(user) +
			" password=" + std::string(password) + " application_name=" + std::string(application_name);

        
        AsyncPostgresPool pool(io_context, conn_str, 5);
		bool bootstrapOK = false;
		bool profilesOK = false;
        auto boostrap = [&]() -> asio::awaitable<void>
            {
                try
                {
                    co_await pool.initialize();
                    spdlog::info("Postgres pool initialized (size: {})", 5);
                    DlmsProfileService::Init(
                        [&pool]() -> boost::asio::awaitable<std::shared_ptr<AsyncDatabase>> {
                            return pool.acquire();
                        });
                    profilesOK = co_await DlmsProfileService::Instance().LoadAllFromDb();
                    bootstrapOK = true;
                }
                catch (const std::exception& e)
                {
					bootstrapError = e.what();
                }
                
                io_context.stop(); // stop the io_context to prevent further operations

            };
		
		boost::asio::co_spawn(io_context, boostrap(), asio::detached);
		io_context.run();
		io_context.restart();

		if (!bootstrapError.empty())
		{
			spdlog::error("Bootstrap failed: {}", bootstrapError);
			ShutdownLogging();
			return 1;
		}

        if (!profilesOK)
        {
			spdlog::warn("DLMS profiles load failed. Starting without cache. Reload with SIGHUP.");
        }

		AsyncPostgresConnector connector = [&pool]() -> boost::asio::awaitable<std::shared_ptr<AsyncDatabase>>
			{
				return pool.acquire();
			};

        Server server(io_context, 8877, connector, secretManager);
        Server server2(io_context, 4059, connector, secretManager); // client meter
        Server zstarServer(io_context, 8880, connector, secretManager); // DCU ZStar

        // signals
        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM, SIGHUP);
        std::function<void(const boost::system::error_code&, int)> on_signal;
        on_signal = [&](const boost::system::error_code& ec, int sig) {
            if (ec) return;

            if (sig == SIGHUP) {
                spdlog::info("[SIGNAL] SIGHUP -> relaod profiles");
                boost::asio::co_spawn(
                    io_context,
                    DlmsProfileService::Instance().Reload(),
                    [](std::exception_ptr ep, bool ok) {
                        if (ep) {
                            try {
                                std::rethrow_exception(ep);
                            }
                            catch (const std::exception& e) {
                                spdlog::error("[DLMS] reload thre:{}", e.what());
                            }
                        }
                        else {
                            spdlog::info("[DLMS] reload {}", ok ? "ok" : "failed");
                        }
                    });
                signals.async_wait(on_signal);
            }
            else
            {
                spdlog::info("[SIGNAL] stop (sig={})", sig);
                io_context.stop();
            }
            };
        signals.async_wait(on_signal);
		
        /**
        funciones para conectarse a dispositivos externos en modo servidor
		DeviceConnector device_connector(io_context,server,connector);
		//ejecutar primera carga
        device_connector.check_and_connect();
        */
		//programar ejecución periódica cada X segundos
		asio::steady_timer timer(io_context, std::chrono::seconds(60));
		std::function<void(const boost::system::error_code&)> timer_handler;
		timer_handler = [&](const boost::system::error_code& ec)
			{
				if (!ec)
				{
					//device_connector.check_and_connect();
					timer.expires_after(std::chrono::seconds(60)); // cada 1 minutos
					timer.async_wait(timer_handler);
                    
				}
			};
		timer.async_wait(timer_handler);
        
        spdlog::info("io_context.run() starting");
        io_context.run();
        spdlog::info("io_context.run() returned; shutting down");

        //cierre ordenado
        pool.shutdown();
        ShutdownLogging();
	}
    catch (std::exception &e)
    {
        std::cerr << "Excepción: " << e.what() << "\n";
		ShutdownLogging();
		return 1;
    }
}