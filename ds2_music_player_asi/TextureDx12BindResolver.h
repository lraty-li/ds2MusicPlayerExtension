#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace TextureDx12BindResolver
{
using BindFn = void(__fastcall*)(uint64_t textureDx12, void* handleSlot);

bool Resolve(HMODULE gameModule, BindFn& bindFn, const Logger& logger);
} // namespace TextureDx12BindResolver
