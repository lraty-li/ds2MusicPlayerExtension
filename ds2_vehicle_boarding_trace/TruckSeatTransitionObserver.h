#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace TruckSeatTransitionObserver {

bool TryInstall(HMODULE gameModule, const Logger& logger);
bool PrepareProcessAttach(uintptr_t rideOn);

} // namespace TruckSeatTransitionObserver
