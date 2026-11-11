#include "writelog_adapter.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <sys/stat.h>
#include <ctime>
#include <string>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <vector>

namespace {

    // -----------------------------------------------------------------------------
    // Estado global (protegido por mutex)
    // -----------------------------------------------------------------------------
    struct ServerLogger {
        std::shared_ptr<spdlog::logger> logger;
        std::string                     filename;   // para detectar cambio de día
        int                             tm_wday;    // día con el que se creó
    };

    std::mutex                                        g_mutex;
    std::unordered_map<int, ServerLogger>             g_loggers;

    // -----------------------------------------------------------------------------
    // Utilidades de tiempo (mismas que ya usas)
    // -----------------------------------------------------------------------------
    static struct tm GetLocaltime()
    {
        std::time_t t = std::time(nullptr);
        struct tm tm_buf {};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        return tm_buf;
    }

    static std::string BuildFilename(int server, const struct tm& now)
    {
        static const char* kDias[] = { "Dom","Lun","Mar","Mie","Jue","Vie","Sab" };
        return "srv-" + std::to_string(server) + "-trace-"
            + kDias[now.tm_wday] + ".txt";
    }

    // Borra el fichero si tiene más de 24h (misma lógica que tu WriteLog)
    static void RemoveIfStale(const std::string& path)
    {
        struct stat result;
        if (::stat(path.c_str(), &result) == 0) {
            std::time_t ahora = std::time(nullptr);
            std::time_t timefile = result.st_mtime;
            double diff = std::difftime(ahora, timefile);
            if (diff > 86400.0)
                std::remove(path.c_str());
        }
    }

    // -----------------------------------------------------------------------------
    // Crea (o recrea) el logger de un server para el día actual.
    // Precondición: g_mutex tomado.
    // -----------------------------------------------------------------------------
    static std::shared_ptr<spdlog::logger>
        CreateLoggerLocked(int server, const struct tm& now)
    {
        const std::string path = BuildFilename(server, now);

        // Igual que tu WriteLog: si el fichero existe y es viejo, se borra
        RemoveIfStale(path);

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, /*truncate=*/false);

        auto logger = std::make_shared<spdlog::async_logger>(
            "srv-" + std::to_string(server),         // nombre único
            sink,
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::block);

        logger->set_level(spdlog::level::trace);      // tu WriteLog no filtraba por nivel
        logger->set_pattern("%v");                    // solo el mensaje, igual que GXHelpers::Write
        logger->flush_on(spdlog::level::trace);       // traza siempre (opcional, ver §6)

        return logger;
    }

} // namespace

// -----------------------------------------------------------------------------
// Inicialización del thread_pool (una sola vez)
// -----------------------------------------------------------------------------
/*void InitLogging()
{
    static std::once_flag once;
    std::call_once(once, []() {
        spdlog::init_thread_pool(8192, 1);
        });
}
*/
// -----------------------------------------------------------------------------
// Fachada pública: sustituye a WriteLog(stringlog, server)
// -----------------------------------------------------------------------------
void WriteLogAsync(std::string stringlog, int server)
{
    const struct tm now = GetLocaltime();

    std::shared_ptr<spdlog::logger> logger;
    {
        std::lock_guard<std::mutex> lk(g_mutex);

        auto it = g_loggers.find(server);
        if (it == g_loggers.end() || it->second.tm_wday != now.tm_wday) {
            // Primer mensaje del server o cambio de día → (re)crear logger
            auto fresh = CreateLoggerLocked(server, now);
            g_loggers[server] = ServerLogger{ fresh, BuildFilename(server, now), now.tm_wday };
            logger = fresh;
        }
        else {
            logger = it->second.logger;
        }
    }

    // El enqueue es rápido: no bloquea salvo que la cola esté llena (block)
    logger->info("{}", stringlog);
}

// -----------------------------------------------------------------------------
// Cierre
// -----------------------------------------------------------------------------
/*void ShutdownLogging()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& [srv, s] : g_loggers) {
        if (s.logger) s.logger->flush();
    }
    g_loggers.clear();
    spdlog::shutdown();
}
*/