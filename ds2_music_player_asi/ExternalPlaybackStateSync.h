#pragma once

#include "Logger.h"

#include <windows.h>

namespace ExternalPlaybackStateSync
{
bool Configure(HMODULE gameModule, const Logger& logger);
void Reset();
void SetRuntime(void* runtime);
bool Poll();
void ApplyPendingOnGameThread();
bool IsApplying();
}
