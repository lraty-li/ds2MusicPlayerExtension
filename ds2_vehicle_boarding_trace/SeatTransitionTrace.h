#pragma once

#include "Logger.h"

#include <windows.h>

namespace SeatTransitionTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace SeatTransitionTrace
