#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace RideOffNativeDetach {

bool TryInstall(HMODULE gameModule, const Logger& logger);
bool Request(uintptr_t runtime);

} // namespace RideOffNativeDetach
