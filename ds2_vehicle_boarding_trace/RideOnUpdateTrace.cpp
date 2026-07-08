#include "pch.h"
#include "RideOnUpdateTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace RideOnUpdateTrace {
namespace {

constexpr const char* kUpdateSignature =
    "40 53 56 57 48 83 EC ? C5 F2 58 81";
constexpr size_t kUpdatePatchLen = 16;

constexpr const char* kDispatchSignature =
    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F B6 B1";
constexpr size_t kDispatchPatchLen = 15;

using UpdateFn = void(__fastcall*)(uintptr_t, float, float);
using DispatchFn = void(__fastcall*)(uintptr_t plugin);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{30};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
UpdateFn g_originalUpdate = nullptr;
DispatchFn g_originalDispatch = nullptr;

void __fastcall HookUpdate(uintptr_t rideOn, float delta, float a3);
void __fastcall HookDispatch(uintptr_t plugin);

uintptr_t FindPattern(const char* sig)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, sig);
}

void __fastcall HookDispatch(uintptr_t plugin)
{
    uint8_t current = 0;
    uint8_t next = 0;
    VehicleSeatTrace::ReadValue(plugin + 0x118, current);
    VehicleSeatTrace::ReadValue(plugin + 0x11A, next);

    g_originalDispatch(plugin);

    // After transition from RideOn(1) to Drive(2), reset animation state
    // This must happen AFTER g_originalDispatch (which performs the transition)
    if (current == 1 && next == 2) {
        // Read the state sub-object that was transitioned to
        uintptr_t driveState = 0;
        uintptr_t rideOn = 0;
        uintptr_t animComp = 0;
        uintptr_t inner = 0;

        // plugin+0x158 = DriveState (from state table: state 2 loads plugin+0x158)
        if (VehicleSeatTrace::ReadValue(plugin + 0x158, driveState) && driveState) {
            // Back-link to the previous RideOn state which has the anim component
            // rideOn might still be valid at plugin+0x150
            if (VehicleSeatTrace::ReadValue(plugin + 0x150, rideOn) && rideOn) {
                if (VehicleSeatTrace::ReadValue(rideOn + 0xB0, animComp) && animComp) {
                    if (VehicleSeatTrace::ReadValue(animComp + 0x8, inner) && inner) {
                        // Write inner animation state to 1 (idle)
                        // This bypasses RebuildTrackSlots and just sets the state
                        VehicleSeatTrace::WriteValue<uint32_t>(inner + 0x2E0, 1);
                        if (g_logBudget.fetch_sub(1) > 0) {
                            g_logger->Log("PostDrive: inner state reset to 1");
                        }
                    }
                }
            }
        }

        if (g_logBudget.fetch_sub(1) > 0) {
            g_logger->Log(
                std::string("DispatchTransition: cur=") +
                std::to_string(current) + " next=" + std::to_string(next) +
                " -> DriveEnter animation reset");
        }
    }
}

void __fastcall HookUpdate(uintptr_t rideOn, float delta, float a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    VehicleSeatTrace::Snapshot before = {};
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);

    g_originalUpdate(rideOn, delta, a3);

    VehicleSeatTrace::Snapshot after = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, after))
        return;

    if (haveBefore && (after.current != before.current || after.next != before.next)) {
        if (g_logBudget.fetch_sub(1) > 0) {
            g_logger->Log(
                std::string("RideOnUpdate ") +
                std::to_string(before.current) + "/" + std::to_string(before.next) +
                " -> " +
                std::to_string(after.current) + "/" + std::to_string(after.next));
        }
    }

    if (after.current == 1 && after.next == 1 && after.stage == 2 &&
        after.b18A && after.b191) {
        if (VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, 2)) {
            g_logger->Log(
                std::string("FastDrive wrote next=2") +
                VehicleSeatTrace::FormatSnapshot(plugin, after));
        }
    }
}

} // anonymous namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;
    logger.Log("RideOnUpdate v0.8.0: DriveEnter anim reset via Dispatch hook");

    auto install = [&](const char* sig, size_t len, const char* name,
                       auto& origFn, void* hookFn) -> bool {
        uintptr_t addr = FindPattern(sig);
        if (!addr) { logger.Log(std::string(name) + " not found"); return false; }
        {
            std::ostringstream oss;
            oss << name << " at " << VehicleSeatTrace::Hex(addr);
            logger.Log(oss.str());
        }
        void* t = JumpHook::MakeTrampoline(addr, len);
        if (!t) return false;
        origFn = decltype(origFn)(t);
        return JumpHook::WriteEntryJump(addr, hookFn, len);
    };

    bool ok = install(kUpdateSignature, kUpdatePatchLen, "RideOnUpdate",
                      g_originalUpdate, (void*)&HookUpdate);
    ok = ok && install(kDispatchSignature, kDispatchPatchLen, "DispatchTransition",
                       g_originalDispatch, (void*)&HookDispatch);

    logger.Log(ok ? "hooks installed" : "install FAILED");
    return ok;
}

} // namespace RideOnUpdateTrace
