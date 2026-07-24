#include "pch.h"

#include "Hooks.h"

#include "FailFast.h"
#include "HookUtils.h"
#include "CustomJacketImageTransfer.h"
#include "Logger.h"
#include "MusicPlayerInjection.h"
#include "PlayStateMonitor.h"
#include "RuntimeEntryTitleRefresh.h"
#include "SpotifyConnectBootstrap.h"
#include "SourceAudioBootstrap.h"

#include <exception>
#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kExpectedModuleName[] = L"DS2.exe";
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
        SourceAudioBootstrap::Configure(gameModule, selfModule);
        LogModuleInfo(selfModule, gameModule);
        RuntimeEntryTitleRefresh::Configure(gameModule);

        if (!gameModule)
        {
            g_initLogger.Log("GetModuleHandleW(\"DS2.exe\") failed");
            FailFast::Now(g_initLogger, "missing DS2.exe module");
        }

        DWORD gameImageSize = 0;
        if (!HookUtils::TryGetModuleSize(gameModule, gameImageSize))
        {
            g_initLogger.Log("failed to read DS2.exe SizeOfImage");
            FailFast::Now(g_initLogger, "failed to read DS2.exe SizeOfImage");
        }

        std::ostringstream sizeLog;
        sizeLog << "DS2.exe SizeOfImage=" << gameImageSize;
        g_initLogger.Log(sizeLog.str());

        CustomJacketImageTransfer::Start(g_initLogger);

        const bool listenerInstalled =
            MusicPlayerInjection::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(listenerInstalled ? "music player listener installed" :
                                             "music player listener install failed");
        if (!listenerInstalled)
        {
            FailFast::Now(g_initLogger, "music player listener install failed");
        }

        const bool playStateMonitorInstalled =
            PlayStateMonitor::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(playStateMonitorInstalled ? "play state monitor installed" :
                                                     "play state monitor install failed");
        if (!playStateMonitorInstalled)
        {
            FailFast::Now(g_initLogger, "play state monitor install failed");
        }

        g_initLogger.Log("source audio registration deferred until music resource load");
        SpotifyConnectBootstrap::Start(selfModule, g_initLogger);
    }
    catch (const std::exception& ex)
    {
        g_initLogger.Log(std::string("exception: ") + ex.what());
        FailFast::Now(g_initLogger, "init thread std::exception");
    }
    catch (...)
    {
        g_initLogger.Log("exception: unknown");
        FailFast::Now(g_initLogger, "init thread unknown exception");
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
