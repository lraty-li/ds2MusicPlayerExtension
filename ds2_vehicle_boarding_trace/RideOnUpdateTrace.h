#pragma once

#include "Logger.h"

#include <windows.h>

namespace RideOnUpdateTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
