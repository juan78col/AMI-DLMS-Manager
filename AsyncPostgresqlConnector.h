#pragma once
#include "AsyncDatabase.h"
#include <functional>
#include <memory>

// Backwards-compatible alias kept for existing code
using AsyncPostgresConnector = std::function<std::shared_ptr<AsyncDatabase>()>;

// New generic connector alias (optional):
using AsyncDatabaseConnector = AsyncPostgresConnector;