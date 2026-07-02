#include "pch.h"
#include "VehicleSeatTrace.h"
#include "JumpHook.h"
#include "RideOnAnimationComponentTrace.h"
#include "SeatTransitionTrace.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace VehicleSeatTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kInitRva = 0x1410047B0ull - kImageBase;
constexpr uintptr_t kAttachRva = 0x140F9A370ull - kImageBase;
constexpr uintptr_t kRideOnExitRva = 0x140F99990ull - kImageBase;
constexpr uintptr_t kDriveEnterRva = 0x140F8EB40ull - kImageBase;
constexpr uintptr_t kClassifyApproachRva = 0x140F9B4A0ull - kImageBase;
constexpr size_t kInitPatchLen = 9;
constexpr size_t kAttachPatchLen = 12;
constexpr size_t kRideOnExitPatchLen = 19;
constexpr size_t kDriveEnterPatchLen = 15;
constexpr size_t kClassifyApproachPatchLen = 15;

using InitFn = char(__fastcall*)(uintptr_t plugin);
using AttachFn = void(__fastcall*)(uintptr_t rideOn);
using RideOnExitFn = int64_t(__fastcall*)(uintptr_t rideOn);
using DriveEnterFn = int64_t(__fastcall*)(uintptr_t driveState, uintptr_t a2, uintptr_t a3);
using ClassifyApproachFn = uint8_t(__fastcall*)(uintptr_t rideOn, uintptr_t seatObject);

std::atomic<bool> g_started{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
InitFn g_originalInit = nullptr;
AttachFn g_originalAttach = nullptr;
RideOnExitFn g_originalRideOnExit = nullptr;
DriveEnterFn g_originalDriveEnter = nullptr;
ClassifyApproachFn g_originalClassifyApproach = nullptr;

char __fastcall HookInit(uintptr_t plugin);
void __fastcall HookAttach(uintptr_t rideOn);
int64_t __fastcall HookRideOnExit(uintptr_t rideOn);
int64_t __fastcall HookDriveEnter(uintptr_t driveState, uintptr_t a2, uintptr_t a3);
uint8_t __fastcall HookClassifyApproach(uintptr_t rideOn, uintptr_t seatObject);

bool InstallInitHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kInitRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kInitPatchLen);
    if (!trampoline)
        return false;
    g_originalInit = reinterpret_cast<InitFn>(trampoline);
    return JumpHook::WriteEntryJump(target, reinterpret_cast<void*>(&HookInit), kInitPatchLen);
}

bool InstallAttachHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kAttachRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kAttachPatchLen);
    if (!trampoline)
        return false;
    g_originalAttach = reinterpret_cast<AttachFn>(trampoline);
    return JumpHook::WriteEntryJump(target, reinterpret_cast<void*>(&HookAttach), kAttachPatchLen);
}

bool InstallRideOnExitHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kRideOnExitRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kRideOnExitPatchLen);
    if (!trampoline)
        return false;
    g_originalRideOnExit = reinterpret_cast<RideOnExitFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookRideOnExit), kRideOnExitPatchLen);
}

bool InstallDriveEnterHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kDriveEnterRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kDriveEnterPatchLen);
    if (!trampoline)
        return false;
    g_originalDriveEnter = reinterpret_cast<DriveEnterFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookDriveEnter), kDriveEnterPatchLen);
}

bool InstallClassifyApproachHook()
{
    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kClassifyApproachRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kClassifyApproachPatchLen);
    if (!trampoline)
        return false;
    g_originalClassifyApproach = reinterpret_cast<ClassifyApproachFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookClassifyApproach), kClassifyApproachPatchLen);
}

char __fastcall HookInit(uintptr_t plugin)
{
    const char result = g_originalInit(plugin);
    if (!result)
        return result;

    Snapshot s = {};
    std::ostringstream oss;
    oss << "Init result=1";
    if (CaptureSnapshot(plugin, s))
        oss << FormatSnapshot(plugin, s);
    else
        oss << " plugin=" << Hex(plugin) << " snapshot=unavailable";
    g_logger->Log(oss.str());
    return result;
}

void LogAttachChange(uintptr_t rideOn, const Snapshot& before, const Snapshot& after)
{
    if (before.stage == after.stage && before.current == after.current &&
        before.next == after.next && before.b18A == after.b18A &&
        before.b18B == after.b18B && before.b189 == after.b189)
        return;

    std::ostringstream oss;
    oss << "Attach rideOn=" << Hex(rideOn)
        << " stage " << before.stage << "->" << after.stage
        << " cur " << static_cast<int>(before.current)
        << "->" << static_cast<int>(after.current)
        << " next " << static_cast<int>(before.next)
        << "->" << static_cast<int>(after.next)
        << " b189 " << static_cast<int>(before.b189)
        << "->" << static_cast<int>(after.b189)
        << " b18A " << static_cast<int>(before.b18A)
        << "->" << static_cast<int>(after.b18A)
        << " b18B " << static_cast<int>(before.b18B)
        << "->" << static_cast<int>(after.b18B)
        << " kind=" << after.rideKind
        << " b3B1=" << static_cast<int>(after.b3B1)
        << " seatKey=" << Hex(after.seatKey);
    g_logger->Log(oss.str());
}

void __fastcall HookAttach(uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    ReadValue(rideOn + 0x88, plugin);
    Snapshot before = {};
    const bool haveBefore = CaptureSnapshot(plugin, before);
    g_originalAttach(rideOn);
    Snapshot after = {};
    if (haveBefore && CaptureSnapshot(plugin, after)) {
        LogAttachChange(rideOn, before, after);
    }
}

int64_t __fastcall HookRideOnExit(uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    ReadValue(rideOn + 0x88, plugin);
    Snapshot before = {};
    if (CaptureSnapshot(plugin, before))
        g_logger->Log(std::string("RideOnExit entry") + FormatSnapshot(plugin, before));

    const int64_t result = g_originalRideOnExit(rideOn);

    Snapshot after = {};
    if (CaptureSnapshot(plugin, after))
        g_logger->Log(std::string("RideOnExit exit") + FormatSnapshot(plugin, after));
    return result;
}

int64_t __fastcall HookDriveEnter(uintptr_t driveState, uintptr_t a2, uintptr_t a3)
{
    uintptr_t plugin = 0;
    ReadValue(driveState + 0x88, plugin);
    Snapshot before = {};
    const bool haveBefore = CaptureSnapshot(plugin, before);
    if (haveBefore)
        g_logger->Log(std::string("DriveEnter entry") + FormatSnapshot(plugin, before));

    const int64_t result = g_originalDriveEnter(driveState, a2, a3);

    Snapshot after = {};
    if (CaptureSnapshot(plugin, after))
        g_logger->Log(std::string("DriveEnter exit") + FormatSnapshot(plugin, after));
    return result;
}

uint8_t __fastcall HookClassifyApproach(uintptr_t rideOn, uintptr_t seatObject)
{
    const uint8_t result = g_originalClassifyApproach(rideOn, seatObject);
    uintptr_t plugin = 0;
    ReadValue(rideOn + 0x88, plugin);
    Snapshot s = {};
    std::ostringstream oss;
    oss << "ClassifyApproach result=" << static_cast<int>(result)
        << " seatObject=" << Hex(seatObject);
    if (CaptureSnapshot(plugin, s))
        oss << FormatSnapshot(plugin, s);
    g_logger->Log(oss.str());
    return result;
}

} // namespace

bool TryProcessAttachImmediately(uintptr_t rideOn)
{
    if (!rideOn || !g_originalAttach)
        return false;

    __try {
        g_originalAttach(rideOn);
        g_originalAttach(rideOn);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;
    logger.Log("VehicleBoard RideOn attach/animation trace hook enabled");

    if (!InstallInitHook()) {
        logger.Log("InstallInitHook failed");
        return false;
    }
    if (!InstallAttachHook()) {
        logger.Log("InstallAttachHook failed");
        return false;
    }
    if (!InstallRideOnExitHook()) {
        logger.Log("InstallRideOnExitHook failed");
        return false;
    }
    if (!InstallDriveEnterHook()) {
        logger.Log("InstallDriveEnterHook failed");
        return false;
    }
    if (!InstallClassifyApproachHook()) {
        logger.Log("InstallClassifyApproachHook failed");
        return false;
    }
    if (!RideOnAnimationComponentTrace::TryInstall(gameModule, logger))
        return false;
    if (!SeatTransitionTrace::TryInstall(gameModule, logger))
        return false;
    return true;
}

} // namespace VehicleSeatTrace
