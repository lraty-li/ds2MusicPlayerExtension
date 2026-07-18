#pragma once

#include "Logger.h"

#include <windows.h>

namespace MoverRootMotionObserver {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace MoverRootMotionObserver
