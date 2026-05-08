#pragma once

#include "Logger.h"

#include <windows.h>

namespace GameSymbols
{
using RegisterPluginDllFn = int(__fastcall*)(const wchar_t* pluginName, const wchar_t* basePath);

struct ResolvedSymbols
{
    RegisterPluginDllFn registerPluginDll = nullptr;
};

ResolvedSymbols Resolve(HMODULE gameModule, const Logger& logger);
} // namespace GameSymbols
