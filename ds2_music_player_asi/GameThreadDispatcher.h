#pragma once

#include "Logger.h"

namespace GameThreadDispatcher
{
using Callback = void(*)();

bool EnsureInstalled(const Logger& logger);
bool Post(Callback callback);
} // namespace GameThreadDispatcher
