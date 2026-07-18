#pragma once

#include "Logger.h"

#include <windows.h>

namespace GraphEventFastForward {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace GraphEventFastForward
