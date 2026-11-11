#pragma once
#include <string>

// Inicializa spdlog asíncrono (thread_pool + logger general por defecto).
// Llamar UNA SOLA VEZ al arranque, antes de cualquier spdlog::info(...)
// y antes de cualquier WriteLogAsync(...).
void InitLogging(const std::string& logFilePath = "/var/log/amidlms/dlms.log");

// Vuelca los logs pendientes y libera recursos. Llamar al final de main().
void ShutdownLogging();