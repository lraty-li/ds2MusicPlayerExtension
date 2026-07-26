#include "PocApp.h"

#include <cwchar>
#include <objbase.h>

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR commandLine,
    int showCommand)
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result))
    {
        return static_cast<int>(result);
    }

    const bool helperMode =
        commandLine && std::wcsstr(commandLine, L"--game-helper");
    PocApp app(helperMode);
    const int exitCode = app.Run(instance, showCommand);
    CoUninitialize();
    return exitCode;
}
