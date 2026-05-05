#pragma once

#include "Logger.h"

#include <windows.h>

namespace WwisePluginRegistration
{
bool TryRegister(HMODULE gameModule, HMODULE selfModule, const Logger& logger);
}
