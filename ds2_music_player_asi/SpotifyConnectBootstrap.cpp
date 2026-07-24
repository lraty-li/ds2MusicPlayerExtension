#include "pch.h"

#include "SpotifyConnectBootstrap.h"

#include "Logger.h"

#include <string>

namespace
{
using BridgeStart = int(WINAPI*)();

HMODULE g_bridgeModule = nullptr;

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
        GetSiblingPath(selfModule, L"DS2SpotifyConnectBridge.dll");
    if (bridgePath.empty() || GetFileAttributesW(bridgePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        logger.Log("spotify connect bridge DLL is missing");
        return;
    }

    if (g_bridgeModule)
    {
        return;
    }

    HMODULE bridgeModule = LoadLibraryW(bridgePath.c_str());
    if (!bridgeModule)
    {
        logger.Log("spotify connect bridge DLL load failed err=" + std::to_string(GetLastError()));
        return;
    }

    const auto start = reinterpret_cast<BridgeStart>(
        GetProcAddress(bridgeModule, "DS2SpotifyConnectBridgeStart"));
    if (!start || start() == 0)
    {
        logger.Log("spotify connect bridge DLL start failed");
        FreeLibrary(bridgeModule);
        return;
    }

    g_bridgeModule = bridgeModule;
    logger.Log("spotify connect bridge loaded in DS2 process");
}
}
