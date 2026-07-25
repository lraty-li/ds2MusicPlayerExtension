#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace RideOffFinalizer {

bool TryInstall(HMODULE gameModule, const Logger& logger);
void Force(uintptr_t runtime, uint8_t specialMode);

} // namespace RideOffFinalizer
