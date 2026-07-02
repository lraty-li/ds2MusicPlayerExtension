#pragma once

#include <windows.h>
#include "Logger.h"

#include <cstdint>

namespace RideOnAnimationComponentTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
bool TryRequestState(uintptr_t animComponent, uintptr_t state);
}
