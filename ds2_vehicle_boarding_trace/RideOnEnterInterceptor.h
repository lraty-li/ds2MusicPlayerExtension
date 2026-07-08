#pragma once

#include "Logger.h"
#include <windows.h>

namespace RideOnEnterInterceptor {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
