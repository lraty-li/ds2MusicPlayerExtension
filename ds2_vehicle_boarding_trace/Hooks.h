#pragma once

#include <windows.h>

namespace Hooks
{
DWORD WINAPI InitThread(LPVOID);
void Shutdown();
} // namespace Hooks
