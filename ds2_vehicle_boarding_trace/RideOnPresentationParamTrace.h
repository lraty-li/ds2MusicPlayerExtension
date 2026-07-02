#pragma once

#include <windows.h>
#include "Logger.h"

namespace RideOnPresentationParamTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
