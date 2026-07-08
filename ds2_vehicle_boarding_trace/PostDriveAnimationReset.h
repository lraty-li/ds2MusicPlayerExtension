#pragma once

#include "Logger.h"
#include <windows.h>

namespace PostDriveAnimationReset {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
