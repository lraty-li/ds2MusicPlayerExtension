#include "pch.h"
#include "RideOnPresentationParamTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOnPresentationParamTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kPresentationParamRva = 0x140E21970ull - kImageBase;
constexpr uintptr_t kRideOnEnterParamRetRva = 0x140F99945ull - kImageBase;
constexpr size_t kPresentationParamPatchLen = 15;

using PresentationParamFn = void(__fastcall*)(uintptr_t ignored, int param, double value);

std::atomic<bool> g_started{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
PresentationParamFn g_originalPresentationParam = nullptr;

void __fastcall HookPresentationParam(uintptr_t ignored, int param, double value)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t expected = reinterpret_cast<uintptr_t>(g_module) + kRideOnEnterParamRetRva;
    if (caller == expected) {
        std::ostringstream oss;
        oss << "RideOnEnterPresentationParam suppressed param="
            << param
            << " caller=" << VehicleSeatTrace::Hex(caller);
        g_logger->Log(oss.str());
        return;
    }
    g_originalPresentationParam(ignored, param, value);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kPresentationParamRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kPresentationParamPatchLen);
    if (!trampoline) {
        logger.Log("InstallRideOnPresentationParamHook trampoline failed");
        return false;
    }
    g_originalPresentationParam = reinterpret_cast<PresentationParamFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookPresentationParam), kPresentationParamPatchLen)) {
        logger.Log("InstallRideOnPresentationParamHook failed");
        return false;
    }
    return true;
}

} // namespace RideOnPresentationParamTrace
