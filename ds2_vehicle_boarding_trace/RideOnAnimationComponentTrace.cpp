#include "pch.h"
#include "RideOnAnimationComponentTrace.h"
#include "JumpHook.h"
#include "RideOnPoseParamTrace.h"
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
constexpr uintptr_t kAnimFloat544SetterRva = 0x140DBA820ull - kImageBase;
constexpr uintptr_t kRideOnEnterSetStateRetRva = 0x140F99892ull - kImageBase;
constexpr size_t kRideOnEnterPatchLen = 16;
constexpr size_t kAnimSetStatePatchLen = 7;
constexpr size_t kAnimFloat544SetterPatchLen = 17;
constexpr bool kSkipRideOnEnterOriginal = false;
constexpr bool kSuppressRideOnEnterAnimState5 = false;

using RideOnEnterFn = int64_t(__fastcall*)(uintptr_t rideOn, double value);
using AnimSetStateFn = void(__fastcall*)(uintptr_t animComponent, uintptr_t state);
using AnimFloatSetterFn = void(__fastcall*)(uintptr_t animComponent, float value);

std::atomic<bool> g_started{false};
std::atomic<int> g_animSetLogBudget{160};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
RideOnEnterFn g_originalRideOnEnter = nullptr;
AnimSetStateFn g_originalAnimSetState = nullptr;
AnimFloatSetterFn g_originalAnimFloat544Setter = nullptr;

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

void LogAnimInnerFields(const char* label, uintptr_t animComponent, uintptr_t state)
{
    uintptr_t inner = 0;
    VehicleSeatTrace::ReadValue(animComponent + 0x8, inner);
    if (!inner)
        return;

    uint32_t v20 = 0;
    uint32_t v2EC = 0;
    uint32_t v398 = 0;
    uint32_t v3A0 = 0;
    uint8_t b3D6 = 0;
    float f544 = 0.0f;
    uintptr_t flags760 = 0;
    VehicleSeatTrace::ReadValue(inner + 0x20, v20);
    VehicleSeatTrace::ReadValue(inner + 0x2EC, v2EC);
    VehicleSeatTrace::ReadValue(inner + 0x398, v398);
    VehicleSeatTrace::ReadValue(inner + 0x3A0, v3A0);
    VehicleSeatTrace::ReadValue(inner + 0x3D6, b3D6);
    VehicleSeatTrace::ReadValue(inner + 0x544, f544);
    VehicleSeatTrace::ReadValue(inner + 0x760, flags760);

    std::ostringstream oss;
    oss << "AnimInner " << label
        << " state=" << state
        << " component=" << VehicleSeatTrace::Hex(animComponent)
        << " inner=" << VehicleSeatTrace::Hex(inner)
        << " v20=" << v20
        << " v2EC=" << v2EC
        << " v398=" << v398
        << " v3A0=" << v3A0
        << " b3D6=" << static_cast<int>(b3D6)
        << " f544=" << f544
        << " flags760=" << VehicleSeatTrace::Hex(flags760);
    g_logger->Log(oss.str());
}

int64_t __fastcall HookRideOnEnter(uintptr_t rideOn, double value)
{
    LogAnimationComponent("entry", rideOn);
    RideOnPoseParamTrace::LogRideOnPoseParams(*g_logger, "entry", rideOn);
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
        //RideOnPoseParamTrace::LogRideOnPoseParams(*g_logger, "skip-exit", rideOn);
        return runtime;
    }
    const int64_t result = g_originalRideOnEnter(rideOn, value);
    LogAnimationComponent("exit", rideOn);
    //RideOnPoseParamTrace::LogRideOnPoseParams(*g_logger, "exit", rideOn);
    return result;
}

void __fastcall HookAnimSetState(uintptr_t animComponent, uintptr_t state)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t expected = reinterpret_cast<uintptr_t>(g_module) + kRideOnEnterSetStateRetRva;
    const bool traceState = state <= 16;
    bool logged = false;
    if (state <= 16) {
        const int remaining = g_animSetLogBudget.fetch_sub(1);
        if (remaining > 0) {
            logged = true;
            std::ostringstream oss;
            oss << "AnimSetState call state=" << state
                << " caller=" << VehicleSeatTrace::Hex(caller)
                << " animComponent=" << VehicleSeatTrace::Hex(animComponent);
            g_logger->Log(oss.str());
            LogAnimInnerFields("before", animComponent, state);
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
    if (traceState && logged)
        LogAnimInnerFields("after", animComponent, state);
}

void __fastcall HookAnimFloat544Setter(uintptr_t animComponent, float value)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    uintptr_t inner = 0;
    float before = 0.0f;
    VehicleSeatTrace::ReadValue(animComponent + 0x8, inner);
    if (inner)
        VehicleSeatTrace::ReadValue(inner + 0x544, before);
    g_originalAnimFloat544Setter(animComponent, value);
    float after = 0.0f;
    if (inner)
        VehicleSeatTrace::ReadValue(inner + 0x544, after);

    std::ostringstream oss;
    oss << "AnimFloat544 caller=" << VehicleSeatTrace::Hex(caller)
        << " component=" << VehicleSeatTrace::Hex(animComponent)
        << " inner=" << VehicleSeatTrace::Hex(inner)
        << " value=" << value
        << " f544 " << before << "->" << after;
    g_logger->Log(oss.str());
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

    const uintptr_t float544Target =
        reinterpret_cast<uintptr_t>(g_module) + kAnimFloat544SetterRva;
    void* float544Trampoline =
        JumpHook::MakeTrampoline(float544Target, kAnimFloat544SetterPatchLen);
    if (!float544Trampoline) {
        logger.Log("InstallAnimFloat544Hook trampoline failed");
        return false;
    }
    g_originalAnimFloat544Setter =
        reinterpret_cast<AnimFloatSetterFn>(float544Trampoline);
    if (!JumpHook::WriteEntryJump(
            float544Target, reinterpret_cast<void*>(&HookAnimFloat544Setter),
            kAnimFloat544SetterPatchLen)) {
        logger.Log("InstallAnimFloat544Hook failed");
        return false;
    }
    return true;
}

} // namespace RideOnAnimationComponentTrace
