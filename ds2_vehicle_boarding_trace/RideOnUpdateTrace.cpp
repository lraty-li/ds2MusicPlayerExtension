#include "pch.h"
#include "RideOnUpdateTrace.h"

#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace RideOnUpdateTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kRideOnUpdateRva = 0x140F99C40ull - kImageBase;
constexpr size_t kRideOnUpdatePatchLen = 16;

using RideOnUpdateFn = void(__fastcall*)(uintptr_t rideOn, float delta, float a3);

std::atomic<bool> g_started{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
RideOnUpdateFn g_originalUpdate = nullptr;

void __fastcall HookRideOnUpdate(uintptr_t rideOn, float delta, float a3);

bool InstallRideOnUpdateHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kRideOnUpdateRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kRideOnUpdatePatchLen);
    if (!trampoline)
        return false;
    g_originalUpdate = reinterpret_cast<RideOnUpdateFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookRideOnUpdate), kRideOnUpdatePatchLen);
}

bool ShouldRequestDrive(const VehicleSeatTrace::Snapshot& s)
{
    return s.current == 1 && s.next == 1 && s.stage == 2 && s.b18A && s.b191;
}

void __fastcall HookRideOnUpdate(uintptr_t rideOn, float delta, float a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    VehicleSeatTrace::Snapshot before = {};
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);

    g_originalUpdate(rideOn, delta, a3);

    VehicleSeatTrace::Snapshot after = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, after))
        return;

    if (haveBefore && before.next != after.next) {
        std::ostringstream oss;
        oss << "RideOnUpdate next changed " << static_cast<int>(before.next)
            << "->" << static_cast<int>(after.next)
            << VehicleSeatTrace::FormatSnapshot(plugin, after);
        g_logger->Log(oss.str());
    }

    if (!ShouldRequestDrive(after))
        return;

    *reinterpret_cast<uint16_t*>(plugin + 0x11A) = 2;
    g_logger->Log(
        std::string("FastDrive requested after RideOnUpdate pose/filter pass") +
        VehicleSeatTrace::FormatSnapshot(plugin, after));
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;
    logger.Log("RideOn update fast-drive hook enabled");
    if (!InstallRideOnUpdateHook()) {
        logger.Log("InstallRideOnUpdateHook failed");
        return false;
    }
    return true;
}

} // namespace RideOnUpdateTrace
