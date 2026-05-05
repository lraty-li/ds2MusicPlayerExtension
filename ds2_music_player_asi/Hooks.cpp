#include "pch.h"

#include "Hooks.h"

#include "HookUtils.h"
#include "Logger.h"
#include "MusicPlayerInjection.h"
#include "PlayStateMonitor.h"
#include "SourcePluginBank.h"
#include "WwisePluginRegistration.h"

#include <exception>
#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kExpectedModuleName[] = L"DS2.exe";
constexpr DWORD kRegisterDelayMs = 10000;

Logger g_dllLogger("DllMain");
Logger g_initLogger("Init");

bool IsCurrentProcessDs2()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
    {
        return false;
    }

    std::wstring fullPath = path;
    const size_t pos = fullPath.find_last_of(L"\\/");
    const std::wstring fileName = pos == std::wstring::npos ?
        fullPath :
        fullPath.substr(pos + 1);
    return _wcsicmp(fileName.c_str(), kExpectedModuleName) == 0;
}

void LogModuleInfo(HMODULE selfModule, HMODULE gameModule)
{
    std::ostringstream oss;
    oss << "gameHandle=" << gameModule
        << " selfHandle=" << selfModule
        << " gamePath=" << HookUtils::NarrowUtf8(HookUtils::GetModulePath(gameModule))
        << " selfPath=" << HookUtils::NarrowUtf8(HookUtils::GetModulePath(selfModule));
    g_initLogger.Log(oss.str());
}
}

DWORD WINAPI Hooks::InitThread(LPVOID moduleParam)
{
    if (!IsCurrentProcessDs2())
    {
        return 0;
    }

    DWORD resetError = ERROR_SUCCESS;
    if (!Logger::ResetLogFile(resetError))
    {
        g_initLogger.Log("ResetLogFile failed err=" + std::to_string(resetError));
    }

    g_dllLogger.Log("DLL_PROCESS_ATTACH");
    g_initLogger.Log("begin stream source plugin registration");

    try
    {
        HMODULE selfModule = reinterpret_cast<HMODULE>(moduleParam);
        HMODULE gameModule = GetModuleHandleW(nullptr);
        LogModuleInfo(selfModule, gameModule);

        if (!gameModule)
        {
            g_initLogger.Log("GetModuleHandleW(\"DS2.exe\") failed");
            return 0;
        }

        DWORD gameImageSize = 0;
        if (!HookUtils::TryGetModuleSize(gameModule, gameImageSize))
        {
            g_initLogger.Log("failed to read DS2.exe SizeOfImage");
            return 0;
        }

        std::ostringstream sizeLog;
        sizeLog << "DS2.exe SizeOfImage=" << gameImageSize;
        g_initLogger.Log(sizeLog.str());

        const bool listenerInstalled =
            MusicPlayerInjection::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(listenerInstalled ? "music player listener installed" :
                                             "music player listener install failed");

        const bool playStateMonitorInstalled =
            PlayStateMonitor::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(playStateMonitorInstalled ? "play state monitor installed" :
                                                     "play state monitor install failed");

        g_initLogger.Log("waiting before RegisterPluginDLL");
        Sleep(kRegisterDelayMs);

        const bool registered =
            WwisePluginRegistration::TryRegister(gameModule, selfModule, g_initLogger);
        g_initLogger.Log(registered ? "stream source plugin registered" :
                                      "stream source plugin registration failed");

        if (registered)
        {
            const int32_t bankResult = SourcePluginBank::TryLoad(gameModule, g_initLogger);
            g_initLogger.Log(bankResult == 1 ? "source plugin bank loaded" :
                                               "source plugin bank load failed");
        }
    }
    catch (const std::exception& ex)
    {
        g_initLogger.Log(std::string("exception: ") + ex.what());
    }
    catch (...)
    {
        g_initLogger.Log("exception: unknown");
    }

    g_initLogger.Log("done");
    return 0;
}

void Hooks::Shutdown()
{
    if (!IsCurrentProcessDs2())
    {
        return;
    }

    g_dllLogger.Log("DLL_PROCESS_DETACH");
}
