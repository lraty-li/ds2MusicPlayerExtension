#pragma once

#include "Logger.h"

#include <windows.h>

namespace RideOffVtableTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace RideOffVtableTrace
