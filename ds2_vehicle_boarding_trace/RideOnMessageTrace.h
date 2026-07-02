#pragma once

#include <windows.h>
#include "Logger.h"

namespace RideOnMessageTrace {
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
