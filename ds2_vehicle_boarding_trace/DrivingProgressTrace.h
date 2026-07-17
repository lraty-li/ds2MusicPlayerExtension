#pragma once

#include "Logger.h"
#include <windows.h>

namespace DrivingProgressTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
