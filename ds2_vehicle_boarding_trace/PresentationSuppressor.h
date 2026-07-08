#pragma once

#include "Logger.h"
#include <windows.h>

namespace PresentationSuppressor {
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
