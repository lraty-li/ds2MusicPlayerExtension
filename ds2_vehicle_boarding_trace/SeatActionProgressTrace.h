#pragma once

#include <windows.h>
#include "Logger.h"

namespace SeatActionProgressTrace {
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
