#pragma once

#include <windows.h>
#include "Logger.h"

namespace SeatActionTrace {
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
