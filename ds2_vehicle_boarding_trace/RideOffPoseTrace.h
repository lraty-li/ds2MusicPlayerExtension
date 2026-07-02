#pragma once
#include <windows.h>

#include "Logger.h"

namespace RideOffPoseTrace {
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
