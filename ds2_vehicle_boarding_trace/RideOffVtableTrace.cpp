#include "pch.h"
#include "RideOffVtableTrace.h"

#include "RideOffMoverSnapshot.h"
#include "RideOffQueueClock.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace RideOffVtableTrace {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSPlayerVehicleRideOffState@@";
constexpr uint32_t kStateEnterSlotIndex = 11;
constexpr uint32_t kStateUpdateSlotIndex = 13;
constexpr uint32_t kStatePresentationSlotIndex = 14;
constexpr float kNativeFallbackElapsedSeconds = 10.1f;

using RideOffEnterFn = int64_t(__fastcall*)(
    uintptr_t rideOff, uintptr_t a2, uintptr_t a3);
using RideOffUpdateFn = char(__fastcall*)(uintptr_t rideOff, float delta);
using RideOffPresentationFn = int64_t(__fastcall*)(
    uintptr_t rideOff, float delta, float presentationDelta);
using AnimStateFn = void(__fastcall*)(uintptr_t animation, uint32_t state);

std::atomic<bool> g_started{false};
std::atomic<uintptr_t> g_activeAnimation{0};
std::atomic<uintptr_t> g_animationSlot{0};
std::atomic<uint64_t> g_enterTick{0};
std::atomic<uint32_t> g_postEndpointUpdateLogs{0};
const Logger* g_logger = nullptr;
RideOffEnterFn g_originalEnter = nullptr;
RideOffUpdateFn g_originalUpdate = nullptr;
RideOffPresentationFn g_originalPresentation = nullptr;
AnimStateFn g_originalAnimState = nullptr;

void AppendCaller(std::ostringstream& oss, uintptr_t caller)
{
    HMODULE callerModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(caller), &callerModule);
    const HMODULE game = GetModuleHandleW(nullptr);
    const HMODULE fullGame = GetModuleHandleW(L"fullgame.dll");
    const char* moduleName = callerModule == game ? "DS2.exe" :
        (callerModule == fullGame ? "fullgame.dll" : "other");
    oss << " caller=" << VehicleSeatTrace::Hex(caller)
        << " callerModule=" << moduleName
        << " callerRva=" << VehicleSeatTrace::Hex(callerModule ?
            caller - reinterpret_cast<uintptr_t>(callerModule) : 0);
}

void AppendSnapshot(std::ostringstream& oss, uintptr_t rideOff)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::Snapshot snapshot = {};
    if (VehicleSeatTrace::ReadValue(rideOff + 0x88, plugin) &&
        VehicleSeatTrace::CaptureSnapshot(plugin, snapshot)) {
        oss << " {" << VehicleSeatTrace::FormatSnapshot(plugin, snapshot)
            << " }";
    }
}

void __fastcall HookAnimState(uintptr_t animation, uint32_t state)
{
    const bool inRideOffCallback = RideOffSession::CurrentId() != 0;
    g_originalAnimState(animation, state);
    if (animation != g_activeAnimation.load(std::memory_order_acquire))
        return;
    std::ostringstream oss;
    oss << "RideOff animation state requested=" << state
        << " callbackScope=" << (inRideOffCallback ? 1 : 0)
        << " endpointCompletion="
        << (RideOffSession::GraphEndpointComplete() ? 1 : 0)
        << " elapsedMs=" << GetTickCount64() -
            g_enterTick.load(std::memory_order_relaxed);
    AppendCaller(oss, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    RideOffMoverSnapshot::Snapshot snapshot = {};
    if (RideOffMoverSnapshot::Capture(animation, snapshot)) {
        oss << " {" << RideOffMoverSnapshot::Format(
            "afterState", snapshot) << "}";
    }
    g_logger->Log(oss.str());
}

bool TryInstallAnimObserver(uintptr_t rideOff)
{
    uintptr_t animation = 0;
    uintptr_t vtable = 0;
    uintptr_t target = 0;
    if (!VehicleSeatTrace::ReadValue(rideOff + 0xB0, animation) || !animation ||
        !VehicleSeatTrace::ReadValue(animation, vtable) || !vtable ||
        !VehicleSeatTrace::ReadValue(vtable + 0x20, target) || !target) {
        return false;
    }

    const uintptr_t slot = vtable + 0x20;
    const uintptr_t installed = g_animationSlot.load(std::memory_order_acquire);
    if (installed && installed != slot) {
        return false;
    }
    g_activeAnimation.store(animation, std::memory_order_release);
    if (installed == slot)
        return true;

    g_originalAnimState = reinterpret_cast<AnimStateFn>(target);
    if (!VtableLocator::SwapSlot(
            slot, target, reinterpret_cast<void*>(&HookAnimState))) {
        return false;
    }
    g_animationSlot.store(slot, std::memory_order_release);
    return true;
}

int64_t __fastcall HookRideOffEnter(
    uintptr_t rideOff, uintptr_t a2, uintptr_t a3)
{
    g_enterTick.store(GetTickCount64(), std::memory_order_relaxed);
    g_postEndpointUpdateLogs.store(0, std::memory_order_relaxed);
    const bool animObserverInstalled = TryInstallAnimObserver(rideOff);
    const int64_t result = g_originalEnter(rideOff, a2, a3);
    uintptr_t player = 0;
    VehicleSeatTrace::ReadValue(rideOff + 0x98, player);
    const uint32_t session = RideOffSession::Begin(rideOff, player);
    std::ostringstream oss;
    oss << "RideOff Enter vtable original result=" << result
        << " rideOff=" << VehicleSeatTrace::Hex(rideOff)
        << " animObserver=" << (animObserverInstalled ? 1 : 0)
        << " session=" << session;
    AppendSnapshot(oss, rideOff);
    g_logger->Log(oss.str());
    return result;
}

char __fastcall HookRideOffUpdate(uintptr_t rideOff, float delta)
{
    const bool postEndpoint = RideOffSession::GraphEndpointComplete();
    const uintptr_t previous = RideOffSession::EnterUpdate(rideOff);
    const uint32_t session = RideOffSession::CurrentId();
    float elapsed = 0.0f;
    float effectiveDelta = delta;
    const bool accelerateStateClock = session &&
        RideOffSession::CompletionReady() &&
        RideOffQueueClock::IsSynchronized(session) &&
        VehicleSeatTrace::ReadValue(rideOff + 0x180, elapsed) &&
        std::isfinite(delta) && delta >= 0.0f && delta <= 1.0f &&
        std::isfinite(elapsed) && elapsed >= 0.0f &&
        elapsed < kNativeFallbackElapsedSeconds;
    if (accelerateStateClock) {
        effectiveDelta += kNativeFallbackElapsedSeconds - elapsed;
    }
    const char result = g_originalUpdate(rideOff, effectiveDelta);
    RideOffSession::LeaveUpdate(previous);
    if (accelerateStateClock) {
        std::ostringstream oss;
        oss << "FastRideOff native fallback clock advanced"
            << " session=" << session
            << " elapsed=" << elapsed
            << " delta=" << delta << "->" << effectiveDelta;
        g_logger->Log(oss.str());
    }
    if (postEndpoint &&
        g_postEndpointUpdateLogs.fetch_add(
            1, std::memory_order_relaxed) < 12) {
        std::ostringstream oss;
        oss << "FastRideOff native post-endpoint Update"
            << " elapsedMs=" << RideOffSession::ElapsedMs()
            << " delta=" << delta
            << " result=" << static_cast<uint32_t>(result);
        AppendSnapshot(oss, rideOff);
        g_logger->Log(oss.str());
    }
    return result;
}

int64_t __fastcall HookRideOffPresentation(
    uintptr_t rideOff, float delta, float presentationDelta)
{
    const uintptr_t previous = RideOffSession::EnterUpdate(rideOff);
    const int64_t result =
        g_originalPresentation(rideOff, delta, presentationDelta);
    uint32_t actionHash = 0;
    if (VehicleSeatTrace::ReadValue(rideOff + 0x1AC, actionHash))
        RideOffSession::ObserveCutInAction(rideOff, actionHash);
    RideOffSession::LeaveUpdate(previous);
    return result;
}

bool ReadSlot(uintptr_t vtable, uint32_t index, uintptr_t& target)
{
    return VehicleSeatTrace::ReadValue(
        vtable + index * sizeof(uintptr_t), target) && target;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;

    VtableLocator::Match enter = {};
    VtableLocator::Match update = {};
    VtableLocator::Match presentation = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0, kStateEnterSlotIndex, enter) ||
        !VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0, kStateUpdateSlotIndex, update) ||
        !VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0, kStatePresentationSlotIndex,
            presentation) ||
        enter.vtable != update.vtable || enter.vtable != presentation.vtable) {
        logger.Log("RideOff vtable RTTI lookup failed");
        return false;
    }

    uintptr_t enterTarget = 0;
    uintptr_t updateTarget = 0;
    uintptr_t presentationTarget = 0;
    if (!ReadSlot(enter.vtable, kStateEnterSlotIndex, enterTarget) ||
        !ReadSlot(enter.vtable, kStateUpdateSlotIndex, updateTarget) ||
        !ReadSlot(enter.vtable, kStatePresentationSlotIndex,
            presentationTarget)) {
        logger.Log("RideOff vtable target read failed");
        return false;
    }

    g_logger = &logger;
    g_originalEnter = reinterpret_cast<RideOffEnterFn>(enterTarget);
    g_originalUpdate = reinterpret_cast<RideOffUpdateFn>(updateTarget);
    g_originalPresentation =
        reinterpret_cast<RideOffPresentationFn>(presentationTarget);
    if (!VtableLocator::SwapSlot(
            enter.slot, enterTarget,
            reinterpret_cast<void*>(&HookRideOffEnter))) {
        logger.Log("RideOff Enter vtable observer install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            update.slot, updateTarget,
            reinterpret_cast<void*>(&HookRideOffUpdate))) {
        VtableLocator::SwapSlot(
            enter.slot, reinterpret_cast<uintptr_t>(&HookRideOffEnter),
            reinterpret_cast<void*>(enterTarget));
        logger.Log("RideOff Update vtable observer install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            presentation.slot, presentationTarget,
            reinterpret_cast<void*>(&HookRideOffPresentation))) {
        VtableLocator::SwapSlot(
            update.slot, reinterpret_cast<uintptr_t>(&HookRideOffUpdate),
            reinterpret_cast<void*>(updateTarget));
        VtableLocator::SwapSlot(
            enter.slot, reinterpret_cast<uintptr_t>(&HookRideOffEnter),
            reinterpret_cast<void*>(enterTarget));
        logger.Log("RideOff RunPresentation vtable observer install failed");
        return false;
    }

    g_started.store(true, std::memory_order_release);
    logger.Log("RideOff staged Enter/Update/RunPresentation hooks installed");
    return true;
}

} // namespace RideOffVtableTrace
