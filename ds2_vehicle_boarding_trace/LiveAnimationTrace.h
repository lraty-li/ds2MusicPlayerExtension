#pragma once

#include "Logger.h"

#include <windows.h>

namespace LiveAnimationTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
