#pragma once

#include "Logger.h"

#include <windows.h>

namespace SourcePluginBank
{
int32_t TryLoad(HMODULE gameModule, const Logger& logger);
} // namespace SourcePluginBank
