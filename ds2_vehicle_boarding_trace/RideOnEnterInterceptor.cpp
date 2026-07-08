#include "pch.h"
#include "RideOnEnterInterceptor.h"
#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"
#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace RideOnEnterInterceptor {
namespace {

constexpr const char* kOnEnterSignature =
    "48 8B C4 48 89 58 ? 48 89 70 ? 57 41 56 41 57 48 81 EC ? ? ? ? "
    "C5 F8 29 70 ? C5 F8 29 78 ? C5 78 29 40 ? 45 33 FF";
constexpr size_t kOnEnterPatchLen = 16;

constexpr const char* kUpdateSignature =
    "40 53 56 57 48 83 EC ? C5 F2 58 81";
constexpr size_t kUpdatePatchLen = 16;

// ProcessVehicleAttach pattern (first match of many)
constexpr const char* kProcAttachSig =
    "49 8D AB 18 FF FF FF 48 81 EC D8 01 00 00 48 8B 05 ? ? ? ? "
    "48 33 C4 48 89 85 ? ? ? ? 48 8B 81 90 01 00 00";
constexpr size_t kProcAttachPatchLen = 16;

using OnEnterFn = uintptr_t(__fastcall*)(uintptr_t rideOn, float a2);
using UpdateFn = void(__fastcall*)(uintptr_t rideOn, float delta, float a3);
using ProcAttachFn = void(__fastcall*)(uintptr_t rideOn);

std::atomic<bool> g_started{ false };
std::atomic<int> g_logBudget{ 40 };
const Logger* g_logger = nullptr;
HMODULE g_module = nullptr;
OnEnterFn g_originalOnEnter = nullptr;
UpdateFn g_originalUpdate = nullptr;
ProcAttachFn g_originalProcAttach = nullptr;

uintptr_t FindPattern(const char* sig) {
    uintptr_t start = 0; size_t size = 0;
    if (!PatternScan::GetSection(g_module, ".text", start, size)) return 0;
    return PatternScan::Find(start, size, sig);
}

void LogOnce(const std::string& msg) {
    if (g_logBudget.fetch_sub(1) > 0) g_logger->Log(msg);
}

// Hook OnEnter: call original, then immediately call ProcessVehicleAttach to
// start entity attachment + seat transition BEFORE the first per-frame update.
uintptr_t __fastcall HookOnEnter(uintptr_t rideOn, float a2) {
    LogOnce("=== HookOnEnter: early attach ===");
    uintptr_t result = g_originalOnEnter(rideOn, a2);

    // Record state before ProcessVehicleAttach
    uint32_t stageBefore = 99;
    VehicleSeatTrace::ReadValue(rideOn + 0x198, stageBefore);

    // Resolve ProcessVehicleAttach from RideOnState vtable slot [27] (offset 0xD8)
    // and call it to advance stage 0→1 (early entity attach + seat transition)
    {
        uintptr_t vtable = 0;
        ProcAttachFn procAttach = nullptr;
        if (VehicleSeatTrace::ReadValue(rideOn, vtable) && vtable) {
            uintptr_t fnAddr = 0;
            if (VehicleSeatTrace::ReadValue(vtable + 0xD8, fnAddr) && fnAddr) {
                procAttach = reinterpret_cast<ProcAttachFn>(fnAddr);
            }
        }
        if (procAttach) {
            procAttach(rideOn);
            LogOnce("  ProcessVehicleAttach called via vtable");
        } else {
            LogOnce("  WARN: Could not resolve ProcAttach from vtable");
        }
    }

    // Record state after
    uint32_t stageAfter = 99;
    uint8_t b18A = 0, b18B = 0;
    uint32_t rideKind = 99;
    uintptr_t runtime = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x198, stageAfter);
    if (VehicleSeatTrace::ReadValue(rideOn + 0x190, runtime) && runtime) {
        VehicleSeatTrace::ReadValue(runtime + 0x18A, b18A);
        VehicleSeatTrace::ReadValue(runtime + 0x18B, b18B);
        VehicleSeatTrace::ReadValue(runtime + 0x2A0, rideKind);

        // Override approach direction to 0 (front) which is instant boarding
        // The game has 3 approaches: 0=front(instant), 1=left(anim), 2=right(anim)
        if (rideKind != 0) {
            VehicleSeatTrace::WriteValue<uint32_t>(runtime + 0x2A0, 0);
            LogOnce(std::string("  Force rideKind ") +
                std::to_string(rideKind) + " -> 0 (front)");
        }
    }
    LogOnce(std::string("  stage ") + std::to_string(stageBefore) +
            " -> " + std::to_string(stageAfter) +
            " b18A=" + std::to_string(b18A) +
            " b18B=" + std::to_string(b18B) +
            " kind=" + std::to_string(rideKind));
    return result;
}

void __fastcall HookUpdate(uintptr_t rideOn, float delta, float a3) {
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    g_originalUpdate(rideOn, delta, a3);
    VehicleSeatTrace::Snapshot after = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, after)) return;
    if (after.current == 1 && after.next == 1 && after.stage == 2 &&
        after.b18A && after.b191) {
        if (VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, 2)) {
            LogOnce(std::string("FastDrive") + VehicleSeatTrace::FormatSnapshot(plugin, after));
        }
    }
}
} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger) {
    if (g_started.exchange(true)) return true;
    g_module = gameModule; g_logger = &logger;
    logger.Log("RideOnEnterInterceptor v0.11.0: early attach");

    auto inst = [&](const char* sig, size_t len, const char* name, auto& orig, void* hook) -> bool {
        uintptr_t addr = FindPattern(sig);
        if (!addr) { logger.Log(std::string(name) + " not found"); return false; }
        std::ostringstream oss; oss << name << " at " << VehicleSeatTrace::Hex(addr); logger.Log(oss.str());
        void* t = JumpHook::MakeTrampoline(addr, len);
        if (!t) return false;
        orig = decltype(orig)(t);
        return JumpHook::WriteEntryJump(addr, hook, len);
    };

    bool ok = inst(kOnEnterSignature, kOnEnterPatchLen, "RideOnEnter",
                   g_originalOnEnter, (void*)&HookOnEnter);
    ok = ok && inst(kUpdateSignature, kUpdatePatchLen, "RideOnUpdate",
                    g_originalUpdate, (void*)&HookUpdate);

    // Don't resolve ProcessVehicleAttach by pattern (too many matches).
    // Instead, we'll read it from the RideOnState vtable at runtime in the hook.

    logger.Log(ok ? "hooks installed" : "install FAILED");
    return ok;
}
} // namespace RideOnEnterInterceptor
