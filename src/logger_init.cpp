#include "logger_init.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

void InitLogging(const std::string& logFilePath)
{
    // Cola asíncrona: 8192 mensajes, 1 hilo de backend.
    // IMPORTANTE: este thread_pool lo comparten TODOS los async_logger,
    // incluido el que crea WriteLogAsync por cada server.
    spdlog::init_thread_pool(8192, 1);

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logFilePath, 10 * 1024 * 1024, 3);
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

    auto logger = std::make_shared<spdlog::async_logger>(
        "dlms",
        spdlog::sinks_init_list{ file_sink, console_sink },
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    logger->set_level(spdlog::level::info);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

    spdlog::set_default_logger(logger);
}

void ShutdownLogging()
{
    spdlog::shutdown();
}