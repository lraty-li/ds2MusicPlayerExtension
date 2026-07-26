#include "pch.h"

#include "Hooks.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE initThread = CreateThread(nullptr, 0, Hooks::InitThread, hModule, 0, nullptr);
        if (initThread)
        {
            CloseHandle(initThread);
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Hooks::Shutdown(reserved != nullptr);
    }

    return TRUE;
}
