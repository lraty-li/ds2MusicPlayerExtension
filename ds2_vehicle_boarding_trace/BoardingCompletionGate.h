#pragma once

#include "Logger.h"
#include <windows.h>

namespace BoardingCompletionGate {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
