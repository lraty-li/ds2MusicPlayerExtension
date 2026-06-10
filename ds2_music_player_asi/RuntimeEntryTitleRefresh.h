#pragma once

#include "Logger.h"

namespace RuntimeEntryTitleRefresh
{
void Configure(HMODULE gameModule, const Logger& logger);
void Reset();
void Request(void* titleText, void* artistText);
void TryApplyPending();
}
