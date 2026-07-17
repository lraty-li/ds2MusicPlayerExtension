#pragma once

#include <cstdint>
#include <windows.h>

namespace CrashTrace {
bool Install(HMODULE gameModule, uint32_t gameImageSize, HMODULE pluginModule);
}
