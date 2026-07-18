#pragma once

#include "Logger.h"

#include <windows.h>

namespace RideOnVtableDiscovery {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace RideOnVtableDiscovery
