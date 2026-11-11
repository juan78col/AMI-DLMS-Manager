#pragma once
#include <string>

// Sustituto asíncrono de WriteLog(stringlog, server).
// Mantiene la misma firma y semántica (fichero por server, rotación diaria,
// borrado a las 24h), pero encola el mensaje en spdlog en vez de escribir
// bloqueando el hilo llamante.
void WriteLogAsync(std::string stringlog, int server);

// Cierra todos los loggers (llamar al final de main antes de salir).
//void ShutdownLogging();

// Inicializa el thread_pool de spdlog. Llamar UNA vez al arranque.
//void InitLogging();