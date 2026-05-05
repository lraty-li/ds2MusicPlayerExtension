#pragma once

#include "Logger.h"

#include <windows.h>

namespace MusicPlayerInjection
{
bool TryInstall(HMODULE gameModule, const Logger& logger);
}
