#include "pch.h"

#include "SpotifyConnectBootstrap.h"

#include "Logger.h"

#include <string>

namespace
{
std::wstring GetSiblingPath(HMODULE module, const wchar_t* name)
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return {};
    }

    std::wstring result(path, length);
    const size_t separator = result.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return {};
    }
    result.resize(separator + 1);
    result += name;
    return result;
}
}

namespace SpotifyConnectBootstrap
{
void Start(HMODULE selfModule, const Logger& logger)
{
    const std::wstring bridgePath =
        GetSiblingPath(selfModule, L"DS2SpotifyConnectBridge.exe");
    if (bridgePath.empty() || GetFileAttributesW(bridgePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        logger.Log("spotify connect bridge executable is missing");
        return;
    }

    std::wstring commandLine = L"\"" + bridgePath + L"\"";
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!CreateProcessW(
            bridgePath.c_str(),
            &commandLine[0],
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo))
    {
        logger.Log("spotify connect bridge start failed err=" + std::to_string(GetLastError()));
        return;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    logger.Log("spotify connect bridge started");
}
}
