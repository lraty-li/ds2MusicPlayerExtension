#pragma once

#include "Logger.h"

#include <windows.h>

namespace PlayStateMonitor
{
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
