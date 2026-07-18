#pragma once

#include "Logger.h"

#include <windows.h>

namespace RideOnUpdateVtableTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace RideOnUpdateVtableTrace
