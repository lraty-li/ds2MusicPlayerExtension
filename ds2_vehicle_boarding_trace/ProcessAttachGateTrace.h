#pragma once

#include "Logger.h"

#include <windows.h>

namespace ProcessAttachGateTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
