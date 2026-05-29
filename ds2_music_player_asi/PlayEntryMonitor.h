#pragma once

#include "Logger.h"

#include <windows.h>

namespace PlayEntryMonitor
{
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
