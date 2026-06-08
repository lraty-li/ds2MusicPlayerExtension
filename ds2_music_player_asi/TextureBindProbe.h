#pragma once

#include "Logger.h"

#include <windows.h>

namespace TextureBindProbe
{
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
