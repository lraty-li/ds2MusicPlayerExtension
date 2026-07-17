#pragma once

#include "Logger.h"
#include <cstdint>
#include <windows.h>

namespace RideOnEnterInterceptor {
bool TryInstall(HMODULE gameModule, const Logger& logger);
uintptr_t ActiveBoardingRideOn();
bool FastBoardingSuppressionActive();
}
