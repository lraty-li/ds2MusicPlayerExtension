#pragma once

#include <windows.h>

namespace RuntimeEntryTitleRefresh
{
void Configure(HMODULE gameModule);
void Reset();
void Request(void* track, void* titleText, void* artistText);
void TryApplyPending();
}
