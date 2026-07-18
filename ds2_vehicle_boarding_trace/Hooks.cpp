#include "pch.h"
#include "Hooks.h"
#include "CrashTrace.h"
#include "CutInCameraFastForward.h"
#include "DriveVtableTrace.h"
#include "FastBoardingSession.h"
#include "FullGameBoardingFastForward.h"
#include "GraphEventFastForward.h"
#include "HookUtils.h"
#include "Logger.h"
#include "MoverRootMotionObserver.h"
#include "RideOnVtableDiscovery.h"
#include "RideOnUpdateVtableTrace.h"

namespace {
constexpr wchar_t kExpectedModuleName[] = L"DS2.exe";
Logger g_logger("VehicleBoard");

bool IsCurrentProcessDs2()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
        return false;
    std::wstring fullPath = path;
    const size_t pos = fullPath.find_last_of(L"\\/");
    const std::wstring fileName = pos == std::wstring::npos ? fullPath : fullPath.substr(pos + 1);
    return _wcsicmp(fileName.c_str(), kExpectedModuleName) == 0;
}
}

DWORD WINAPI Hooks::InitThread(LPVOID moduleParam)
{
    if (!IsCurrentProcessDs2())
        return 0;

    DWORD resetErr = ERROR_SUCCESS;
    Logger::ResetLogFile(resetErr);

    HMODULE gameModule = GetModuleHandleW(nullptr);
    g_logger.Log("VehicleBoard FAST BOARDING MOD v2.0.0 start");

    if (!gameModule) {
        g_logger.Log("GetModuleHandleW failed");
        return 1;
    }

    DWORD imageSize = 0;
    if (!HookUtils::TryGetModuleSize(gameModule, imageSize)) {
        g_logger.Log("TryGetModuleSize failed");
        return 1;
    }
    if (!CrashTrace::Install(
            gameModule, imageSize, reinterpret_cast<HMODULE>(moduleParam))) {
        g_logger.Log("CrashTrace install failed");
    }

    const bool rideOnInstalled =
        RideOnVtableDiscovery::TryInstall(gameModule, g_logger);
    if (!rideOnInstalled)
        g_logger.Log("RideOn vtable observer failed");
    const bool driveInstalled =
        DriveVtableTrace::TryInstall(gameModule, g_logger);
    if (!driveInstalled)
        g_logger.Log("Drive vtable observer failed");
    const bool updateInstalled =
        RideOnUpdateVtableTrace::TryInstall(gameModule, g_logger);
    if (!updateInstalled)
        g_logger.Log("RideOn Update vtable observer failed");
    if (rideOnInstalled && driveInstalled && updateInstalled) {
        FastBoardingSession::ReportComponentReady(
            FastBoardingSession::kScopeComponent);
    }
    if (!GraphEventFastForward::TryInstall(gameModule, g_logger))
        g_logger.Log("FastBoarding graph event wrapper failed");
    if (!CutInCameraFastForward::TryInstall(gameModule, g_logger))
        g_logger.Log("FastBoarding CutIn wrapper failed");
    if (!MoverRootMotionObserver::TryInstall(gameModule, g_logger))
        g_logger.Log("FastBoarding Mover observer failed");

    const bool ok = FullGameBoardingFastForward::TryInstall(g_logger);
    g_logger.Log(ok ? "hooks installed" : "hook install FAILED");
    return 0;
}

void Hooks::Shutdown()
{
    if (!IsCurrentProcessDs2())
        return;
    g_logger.Log("DLL_PROCESS_DETACH");
}
