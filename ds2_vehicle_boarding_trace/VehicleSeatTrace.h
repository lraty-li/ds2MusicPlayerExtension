#pragma once
#include <windows.h>
#include "Logger.h"

namespace VehicleSeatTrace {
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
