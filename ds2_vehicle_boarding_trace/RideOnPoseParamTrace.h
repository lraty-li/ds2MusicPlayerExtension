#pragma once
#include <windows.h>

#include "Logger.h"

namespace RideOnPoseParamTrace {
    void LogRideOnPoseParams(const Logger& logger, const char* label, uintptr_t rideOn);
}
