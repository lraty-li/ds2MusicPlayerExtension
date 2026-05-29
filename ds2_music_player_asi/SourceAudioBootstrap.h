#pragma once

#include "Logger.h"

#include <windows.h>

namespace SourceAudioBootstrap
{
void Configure(HMODULE gameModule, HMODULE selfModule);
bool EnsureReady(const Logger& logger, DWORD timeoutMs);
}
