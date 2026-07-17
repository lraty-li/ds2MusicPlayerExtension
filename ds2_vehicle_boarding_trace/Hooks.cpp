#include "pch.h"
#include "Hooks.h"
#include "BoardingCompletionGate.h"
#include "CrashTrace.h"
#include "DrivingProgressTrace.h"
#include "FullGameAnimationTrace.h"
#include "FullGameResultTrace.h"
#include "HookUtils.h"
#include "Logger.h"
#include "RideOnEnterInterceptor.h"
#include "RideOnUpdateTrace.h"
#include "SeatStateTrace.h"
#include "SeatTransitionTrace.h"
#include <sstream>

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
    g_logger.Log("VehicleBoard ANIMATION OWNER TRACE v0.9.2 start");

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

    const bool stateFlowOk = RideOnEnterInterceptor::TryInstall(gameModule, g_logger);
    const bool fastUpdateOk = RideOnUpdateTrace::TryInstall(gameModule, g_logger);
    const bool completionGateOk = BoardingCompletionGate::TryInstall(gameModule, g_logger);
    const bool drivingProgressOk = DrivingProgressTrace::TryInstall(gameModule, g_logger);
    const bool seatStateOk = SeatStateTrace::TryInstall(gameModule, g_logger);
    const bool seatTransitionOk = SeatTransitionTrace::TryInstall(gameModule, g_logger);
    const bool fullGameAnimationOk = FullGameAnimationTrace::TryInstall(g_logger);
    const bool fullGameResultOk = FullGameResultTrace::TryInstall(g_logger);
    const bool ok = stateFlowOk && fastUpdateOk && completionGateOk && drivingProgressOk &&
        seatStateOk && seatTransitionOk && fullGameAnimationOk && fullGameResultOk;
    g_logger.Log(ok ? "hooks installed" : "hook install FAILED");
    return 0;
}

void Hooks::Shutdown()
{
    if (!IsCurrentProcessDs2())
        return;
    g_logger.Log("DLL_PROCESS_DETACH");
}
