#include "pch.h"
#include "PostDriveAnimationReset.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace PostDriveAnimationReset {
namespace {

// Match DSPlayerVehicleDriveState_OnEnter
// Prologue: push rdi; push r14; sub rsp,? ; mov rax,[rcx+?] ; mov rdi,rcx ; mov rax,[rax+?]
constexpr const char* kDriveEnterSignature =
    "40 57 41 56 48 83 EC ? 48 8B 81 ? ? ? ? 48 8B F9 48 8B 88";
constexpr size_t kDriveEnterPatchLen = 15;

using DriveEnterFn = int64_t(__fastcall*)(uintptr_t driveState, uintptr_t a2, uintptr_t a3);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{4};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
DriveEnterFn g_originalDriveEnter = nullptr;

uintptr_t ResolveDriveEnter()
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, kDriveEnterSignature);
}

void SetAnimState(uintptr_t rideOn, uint32_t state)
{
    uintptr_t anim = 0;
    uintptr_t vtbl = 0;
    uintptr_t setStateFn = 0;

    if (!VehicleSeatTrace::ReadValue(rideOn + 0xB0, anim) || !anim)
        return;
    if (!VehicleSeatTrace::ReadValue(anim, vtbl) || !vtbl)
        return;
    if (!VehicleSeatTrace::ReadValue(vtbl + 0x20, setStateFn) || !setStateFn)
        return;

    __try {
        reinterpret_cast<void(__fastcall*)(uintptr_t, uint32_t)>(setStateFn)(anim, state);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

int64_t __fastcall HookDriveEnter(uintptr_t driveState, uintptr_t a2, uintptr_t a3)
{
    const int64_t result = g_originalDriveEnter(driveState, a2, a3);

    // After Drive Enter: reset the RideOn animation component back to
    // state 1 (idle/drive pose). Without this, the animation stays in
    // state 5 (boarding) and the entrance animation clips keep playing,
    // making it look like the full boarding animation still runs even
    // though the state machine has already transitioned to Drive.
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(driveState + 0x88, plugin);
    uintptr_t rideOn = 0;
    if (VehicleSeatTrace::ReadValue(plugin + 0x150, rideOn) && rideOn) {
        SetAnimState(rideOn, 1);
        if (g_logBudget.fetch_sub(1) > 0) {
            g_logger->Log("PostDrive anim state reset to 1");
        }
    }

    return result;
}

} // anonymous namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = ResolveDriveEnter();
    if (!target) {
        logger.Log("DriveEnter signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "DriveEnter resolved at " << VehicleSeatTrace::Hex(target);
    logger.Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kDriveEnterPatchLen);
    if (!trampoline) {
        logger.Log("DriveEnter trampoline failed");
        return false;
    }

    g_originalDriveEnter = reinterpret_cast<DriveEnterFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookDriveEnter), kDriveEnterPatchLen)) {
        logger.Log("DriveEnter hook failed");
        return false;
    }

    return true;
}

} // namespace PostDriveAnimationReset
