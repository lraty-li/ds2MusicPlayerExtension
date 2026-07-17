#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace SeatTransitionTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger);
uintptr_t ActiveSeatController();

} // namespace SeatTransitionTrace
