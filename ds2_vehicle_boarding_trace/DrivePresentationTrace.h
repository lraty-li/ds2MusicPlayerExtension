#pragma once

#include "Logger.h"

#include <windows.h>

namespace DrivePresentationTrace {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
