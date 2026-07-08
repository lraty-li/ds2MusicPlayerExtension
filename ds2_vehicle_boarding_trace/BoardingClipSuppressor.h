#pragma once

#include "Logger.h"
#include <windows.h>

namespace BoardingClipSuppressor {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
