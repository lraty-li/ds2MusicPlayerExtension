#pragma once

#include "Logger.h"

#include <windows.h>

namespace TextureUploadProbe
{
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
