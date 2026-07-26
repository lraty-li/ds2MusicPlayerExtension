#include "PocApp.h"

#include <cstdlib>
#include <cwchar>
#include <objbase.h>
#include <shellapi.h>

namespace
{
struct LaunchOptions
{
    bool helperMode = false;
    DWORD gameProcessId = 0;
};

LaunchOptions ParseLaunchOptions()
{
    LaunchOptions options;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
    {
        return options;
    }
    for (int index = 1; index < count; ++index)
    {
        if (wcscmp(arguments[index], L"--game-helper") == 0)
        {
            options.helperMode = true;
        }
        else if (wcscmp(arguments[index], L"--game-pid") == 0 &&
                 index + 1 < count)
        {
            wchar_t* end = nullptr;
            const unsigned long value =
                wcstoul(arguments[++index], &end, 10);
            if (end && *end == L'\0' && value <= MAXDWORD)
            {
                options.gameProcessId = static_cast<DWORD>(value);
            }
        }
    }
    LocalFree(arguments);
    return options;
}
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(result))
    {
        return static_cast<int>(result);
    }

    const LaunchOptions options = ParseLaunchOptions();
    PocApp app(options.helperMode, options.gameProcessId);
    const int exitCode = app.Run(instance, showCommand);
    CoUninitialize();
    return exitCode;
}
