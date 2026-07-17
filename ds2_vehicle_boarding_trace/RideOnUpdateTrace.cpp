#include "pch.h"
#include "RideOnUpdateTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOnUpdateTrace {
namespace {

constexpr const char* kUpdateSignature =
    "40 53 56 57 48 83 EC ? C5 F2 58 81";
constexpr size_t kUpdatePatchLen = 16;

using UpdateFn = void(__fastcall*)(uintptr_t rideOn, float delta, float a3);

std::atomic<bool> g_started{false};
std::atomic<bool> g_requestedDrive{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
UpdateFn g_originalUpdate = nullptr;

void __fastcall HookUpdate(uintptr_t rideOn, float delta, float a3)
{
    g_originalUpdate(rideOn, delta, a3);

    uintptr_t plugin = 0;
    VehicleSeatTrace::Snapshot snapshot = {};
    if (!VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin) ||
        !VehicleSeatTrace::CaptureSnapshot(plugin, snapshot)) {
        return;
    }
    if (snapshot.current == 1 && snapshot.next == 1 && snapshot.stage < 2)
        g_requestedDrive.store(false);
    const bool readyForDrive =
        RideOnEnterInterceptor::FastBoardingSuppressionActive() &&
        snapshot.current == 1 && snapshot.next == 1 &&
        snapshot.stage == 2 && snapshot.b18A && snapshot.b18B && snapshot.b191;
    if (!readyForDrive || g_requestedDrive.exchange(true))
        return;

    if (VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, 2)) {
        g_logger->Log(
            std::string("FastBoarding requested Drive after native gate") +
            VehicleSeatTrace::FormatSnapshot(plugin, snapshot));
    }
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;
    g_module = gameModule;
    g_logger = &logger;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return false;
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kUpdateSignature);
    if (!target) {
        logger.Log("FastBoarding RideOnUpdate signature not unique");
        return false;
    }
    void* trampoline = JumpHook::MakeTrampoline(target, kUpdatePatchLen);
    if (!trampoline)
        return false;
    g_originalUpdate = reinterpret_cast<UpdateFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookUpdate), kUpdatePatchLen)) {
        return false;
    }
    std::ostringstream oss;
    oss << "FastBoarding RideOnUpdate hook installed at "
        << VehicleSeatTrace::Hex(target);
    logger.Log(oss.str());
    return true;
}

} // namespace RideOnUpdateTrace
