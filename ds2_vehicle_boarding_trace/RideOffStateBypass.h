#pragma once

#include "Logger.h"

#include <windows.h>

namespace RideOffStateBypass {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace RideOffStateBypass
