#include "pch.h"
#include "RideOnAnimationComponentTrace.h"
#include "JumpHook.h"
#include "VehicleSeatTrace.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOnAnimationComponentTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kRideOnEnterRva = 0x140F98CE0ull - kImageBase;
constexpr uintptr_t kAnimSetStateWrapperRva = 0x140DB9A10ull - kImageBase;
constexpr uintptr_t kRideOnEnterSetStateRetRva = 0x140F99892ull - kImageBase;
constexpr size_t kRideOnEnterPatchLen = 16;
constexpr size_t kAnimSetStatePatchLen = 7;
constexpr bool kSkipRideOnEnterOriginal = true;
constexpr bool kSuppressRideOnEnterAnimState5 = false;

using RideOnEnterFn = int64_t(__fastcall*)(uintptr_t rideOn, double value);
using AnimSetStateFn = void(__fastcall*)(uintptr_t animComponent, uintptr_t state);

std::atomic<bool> g_started{false};
std::atomic<int> g_animSetLogBudget{160};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
RideOnEnterFn g_originalRideOnEnter = nullptr;
AnimSetStateFn g_originalAnimSetState = nullptr;

template <typename T>
void WriteField(uintptr_t addr, T value)
{
    __try {
        *reinterpret_cast<T*>(addr) = value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void LogAnimationComponent(const char* label, uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    uintptr_t animComponent = 0;
    uintptr_t vtbl = 0;
    uintptr_t resetFn = 0;
    uintptr_t setStateFn = 0;
    uintptr_t inner = 0;
    uintptr_t innerVtable = 0;
    uintptr_t innerSetStateFn = 0;

    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    VehicleSeatTrace::ReadValue(rideOn + 0xB0, animComponent);
    if (animComponent) {
        VehicleSeatTrace::ReadValue(animComponent, vtbl);
        VehicleSeatTrace::ReadValue(vtbl + 0x250, resetFn);
        VehicleSeatTrace::ReadValue(vtbl + 0x20, setStateFn);
        VehicleSeatTrace::ReadValue(animComponent + 0x8, inner);
        if (inner) {
            VehicleSeatTrace::ReadValue(inner, innerVtable);
            VehicleSeatTrace::ReadValue(innerVtable + 0x168, innerSetStateFn);
        }
    }

    std::ostringstream oss;
    oss << "RideOnEnter " << label
        << " rideOn=" << VehicleSeatTrace::Hex(rideOn)
        << " plugin=" << VehicleSeatTrace::Hex(plugin)
        << " animComponent=" << VehicleSeatTrace::Hex(animComponent)
        << " vtbl=" << VehicleSeatTrace::Hex(vtbl)
        << " fn250=" << VehicleSeatTrace::Hex(resetFn)
        << " fn20=" << VehicleSeatTrace::Hex(setStateFn)
        << " inner=" << VehicleSeatTrace::Hex(inner)
        << " innerVtable=" << VehicleSeatTrace::Hex(innerVtable)
        << " innerFn168=" << VehicleSeatTrace::Hex(innerSetStateFn);
    g_logger->Log(oss.str());
}

int64_t __fastcall HookRideOnEnter(uintptr_t rideOn, double value)
{
    LogAnimationComponent("entry", rideOn);
    if (kSkipRideOnEnterOriginal) {
        uintptr_t plugin = 0;
        uintptr_t runtime = 0;
        VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
        VehicleSeatTrace::ReadValue(rideOn + 0x190, runtime);
        WriteField<uint32_t>(rideOn + 0x180, 0);
        WriteField<uint32_t>(rideOn + 0x198, 0);
        if (runtime) {
            WriteField<uint8_t>(runtime + 0x191, 1);
            WriteField<uint32_t>(runtime + 0x2A0, 1);
            WriteField<uint8_t>(runtime + 0x3B0, 1);
        }
        const bool attached = VehicleSeatTrace::TryProcessAttachImmediately(rideOn);
        VehicleSeatTrace::Snapshot s = {};
        if (plugin && VehicleSeatTrace::CaptureSnapshot(plugin, s) && s.stage == 2) {
            WriteField<uint16_t>(plugin + 0x11A, 2);
        }
        std::ostringstream oss;
        oss << "RideOnEnter original skipped; direct attach attempted="
            << (attached ? 1 : 0);
        if (plugin && VehicleSeatTrace::CaptureSnapshot(plugin, s))
            oss << VehicleSeatTrace::FormatSnapshot(plugin, s);
        g_logger->Log(oss.str());
        LogAnimationComponent("skip-exit", rideOn);
        return runtime;
    }
    const int64_t result = g_originalRideOnEnter(rideOn, value);
    LogAnimationComponent("exit", rideOn);
    return result;
}

void __fastcall HookAnimSetState(uintptr_t animComponent, uintptr_t state)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t expected = reinterpret_cast<uintptr_t>(g_module) + kRideOnEnterSetStateRetRva;
    if (state <= 16) {
        const int remaining = g_animSetLogBudget.fetch_sub(1);
        if (remaining > 0) {
            std::ostringstream oss;
            oss << "AnimSetState call state=" << state
                << " caller=" << VehicleSeatTrace::Hex(caller)
                << " animComponent=" << VehicleSeatTrace::Hex(animComponent);
            g_logger->Log(oss.str());
        }
    }
    if (kSuppressRideOnEnterAnimState5 && caller == expected && state == 5) {
        std::ostringstream oss;
        oss << "RideOnEnterAnimSetState suppressed state=" << state
            << " animComponent=" << VehicleSeatTrace::Hex(animComponent);
        g_logger->Log(oss.str());
        return;
    }
    g_originalAnimSetState(animComponent, state);
}

} // namespace

bool TryRequestState(uintptr_t animComponent, uintptr_t state)
{
    if (!animComponent || !g_originalAnimSetState)
        return false;

    __try {
        g_originalAnimSetState(animComponent, state);
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

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kRideOnEnterRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kRideOnEnterPatchLen);
    if (!trampoline) {
        logger.Log("InstallRideOnEnterTrace trampoline failed");
        return false;
    }
    g_originalRideOnEnter = reinterpret_cast<RideOnEnterFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookRideOnEnter), kRideOnEnterPatchLen)) {
        logger.Log("InstallRideOnEnterTrace failed");
        return false;
    }

    const uintptr_t setStateTarget =
        reinterpret_cast<uintptr_t>(g_module) + kAnimSetStateWrapperRva;
    void* setStateTrampoline =
        JumpHook::MakeTrampoline(setStateTarget, kAnimSetStatePatchLen);
    if (!setStateTrampoline) {
        logger.Log("InstallRideOnAnimSetStateHook trampoline failed");
        return false;
    }
    g_originalAnimSetState = reinterpret_cast<AnimSetStateFn>(setStateTrampoline);
    if (!JumpHook::WriteEntryJump(
            setStateTarget, reinterpret_cast<void*>(&HookAnimSetState),
            kAnimSetStatePatchLen)) {
        logger.Log("InstallRideOnAnimSetStateHook failed");
        return false;
    }
    return true;
}

} // namespace RideOnAnimationComponentTrace
