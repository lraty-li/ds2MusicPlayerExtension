#include "pch.h"
#include "DrivePresentationTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace DrivePresentationTrace {
namespace {

constexpr const char* kDriveEnterSignature =
    "40 57 41 56 48 83 EC ? 48 8B 81 ? ? ? ? 48 8B F9 48 8B 88";
constexpr const char* kPresentationRequestSignature =
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? "
    "48 8D B9 ? ? ? ? 48 8B D9 48 8B CF 41 8B E9";
constexpr size_t kDriveEnterPatchLen = 15;
constexpr size_t kPresentationPatchLen = 15;
constexpr uint32_t kRideOnBoardingPresentationAction = 0x53758BEDu;

using DriveEnterFn = int64_t(__fastcall*)(uintptr_t drive, uintptr_t a2, uintptr_t a3);
using PresentationRequestFn = void(__fastcall*)(
    uintptr_t global, uint32_t actionHash, uintptr_t a3,
    int32_t a4, uintptr_t target, uint8_t force);
using AnimStateRequestFn = int64_t(__fastcall*)(uintptr_t animComponent, uint32_t state);

std::atomic<bool> g_started{false};
std::atomic<int> g_driveLogBudget{12};
std::atomic<int> g_presentationLogBudget{24};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
DriveEnterFn g_originalDriveEnter = nullptr;
PresentationRequestFn g_originalPresentationRequest = nullptr;

uintptr_t ResolveTextSignature(const char* signature)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, signature);
}

void LogDriveState(const char* label, uintptr_t drive)
{
    if (g_driveLogBudget.fetch_sub(1) <= 0)
        return;

    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(drive + 0x88, plugin);
    VehicleSeatTrace::Snapshot s = {};

    std::ostringstream oss;
    oss << "DriveEnter " << label
        << " drive=" << VehicleSeatTrace::Hex(drive);
    if (VehicleSeatTrace::CaptureSnapshot(plugin, s))
        oss << VehicleSeatTrace::FormatSnapshot(plugin, s);
    else
        oss << " plugin=" << VehicleSeatTrace::Hex(plugin)
            << " snapshot=unavailable";
    g_logger->Log(oss.str());
}

bool TryRequestAnimState(uintptr_t requestState, uintptr_t anim, uint32_t state)
{
    __try {
        if (!requestState)
            return false;
        reinterpret_cast<AnimStateRequestFn>(requestState)(anim, state);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void RequestPostDriveAnimState(uintptr_t drive)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(drive + 0x88, plugin);

    VehicleSeatTrace::Snapshot s = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, s))
        return;

    uintptr_t anim = 0;
    uintptr_t vtbl = 0;
    uintptr_t requestState = 0;
    VehicleSeatTrace::ReadValue(s.rideOn + 0xB0, anim);
    VehicleSeatTrace::ReadValue(anim, vtbl);
    VehicleSeatTrace::ReadValue(vtbl + 0x20, requestState);

    const bool called = TryRequestAnimState(requestState, anim, 1);

    std::ostringstream oss;
    oss << "PostDriveAnimState state=1 called=" << (called ? 1 : 0)
        << " anim=" << VehicleSeatTrace::Hex(anim)
        << " fn20=" << VehicleSeatTrace::Hex(requestState)
        << VehicleSeatTrace::FormatSnapshot(plugin, s);
    g_logger->Log(oss.str());
}

int64_t __fastcall HookDriveEnter(uintptr_t drive, uintptr_t a2, uintptr_t a3)
{
    LogDriveState("entry", drive);
    const int64_t result = g_originalDriveEnter(drive, a2, a3);
    LogDriveState("exit", drive);
    RequestPostDriveAnimState(drive);
    return result;
}

void __fastcall HookPresentationRequest(
    uintptr_t global, uint32_t actionHash, uintptr_t a3,
    int32_t a4, uintptr_t target, uint8_t force)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (g_presentationLogBudget.fetch_sub(1) > 0) {
        std::ostringstream oss;
        oss << "PresentationRequest"
            << " caller=" << VehicleSeatTrace::Hex(caller)
            << " action=0x" << std::hex << actionHash << std::dec
            << " a4=" << a4
            << " target=" << VehicleSeatTrace::Hex(target)
            << " force=" << static_cast<int>(force)
            << " global=" << VehicleSeatTrace::Hex(global);
        if (actionHash == kRideOnBoardingPresentationAction)
            oss << " suppressed=1";
        g_logger->Log(oss.str());
    }
    if (actionHash == kRideOnBoardingPresentationAction)
        return;

    g_originalPresentationRequest(global, actionHash, a3, a4, target, force);
}

bool InstallDriveEnter()
{
    const uintptr_t target = ResolveTextSignature(kDriveEnterSignature);
    if (!target) {
        g_logger->Log("DriveEnter signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "DriveEnter resolved at " << VehicleSeatTrace::Hex(target);
    g_logger->Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kDriveEnterPatchLen);
    if (!trampoline)
        return false;
    g_originalDriveEnter = reinterpret_cast<DriveEnterFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookDriveEnter), kDriveEnterPatchLen);
}

bool InstallPresentationRequest()
{
    const uintptr_t target = ResolveTextSignature(kPresentationRequestSignature);
    if (!target) {
        g_logger->Log("PresentationRequest signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "PresentationRequest resolved at " << VehicleSeatTrace::Hex(target);
    g_logger->Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kPresentationPatchLen);
    if (!trampoline)
        return false;
    g_originalPresentationRequest =
        reinterpret_cast<PresentationRequestFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookPresentationRequest),
        kPresentationPatchLen);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    if (!InstallDriveEnter())
        return false;
    return InstallPresentationRequest();
}

} // namespace DrivePresentationTrace
