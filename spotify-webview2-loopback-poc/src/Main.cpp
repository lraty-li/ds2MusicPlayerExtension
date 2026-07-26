#include "PocApp.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result))
    {
        return static_cast<int>(result);
    }

    PocApp app;
    const int exitCode = app.Run(instance, showCommand);
    CoUninitialize();
    return exitCode;
}
