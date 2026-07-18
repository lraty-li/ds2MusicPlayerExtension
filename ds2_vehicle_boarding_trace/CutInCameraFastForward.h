#pragma once

#include "Logger.h"

#include <windows.h>

namespace CutInCameraFastForward {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace CutInCameraFastForward
