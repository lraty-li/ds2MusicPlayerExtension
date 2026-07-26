#include "pch.h"
#include "RideOffVtableTrace.h"

#include "RideOffFinalizer.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOffVtableTrace {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSPlayerVehicleRideOffState@@";
constexpr uint32_t kStateEnterSlotIndex = 11;
constexpr uint32_t kStateUpdateSlotIndex = 13;
constexpr uint32_t kStatePresentationSlotIndex = 14;
constexpr uintptr_t kAnimationReadyOffset = 0x3E0;

using RideOffEnterFn = int64_t(__fastcall*)(
    uintptr_t rideOff, uintptr_t a2, uintptr_t a3);
using RideOffUpdateFn = char(__fastcall*)(uintptr_t rideOff, float delta);
using RideOffPresentationFn = int64_t(__fastcall*)(
    uintptr_t rideOff, float delta, float presentationDelta);
using AnimStateFn = void(__fastcall*)(uintptr_t animation, uint32_t state);
using AnimReadyFn = bool(__fastcall*)(uintptr_t animation);

std::atomic<bool> g_started{false};
std::atomic<uintptr_t> g_activeAnimation{0};
std::atomic<uintptr_t> g_animationSlot{0};
std::atomic<uintptr_t> g_animationReadySlot{0};
std::atomic<uint64_t> g_enterTick{0};
const Logger* g_logger = nullptr;
RideOffEnterFn g_originalEnter = nullptr;
RideOffUpdateFn g_originalUpdate = nullptr;
RideOffPresentationFn g_originalPresentation = nullptr;
AnimStateFn g_originalAnimState = nullptr;
AnimReadyFn g_originalAnimReady = nullptr;

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
        << " elapsedMs=" << GetTickCount64() -
            g_enterTick.load(std::memory_order_relaxed);
    g_logger->Log(oss.str());
}

bool __fastcall HookAnimReady(uintptr_t animation)
{
    if (animation == g_activeAnimation.load(std::memory_order_acquire) &&
        RideOffSession::CurrentId()) {
        return true;
    }
    return g_originalAnimReady(animation);
}

bool TryInstallAnimObserver(uintptr_t rideOff)
{
    uintptr_t animation = 0;
    uintptr_t vtable = 0;
    uintptr_t target = 0;
    uintptr_t readyTarget = 0;
    if (!VehicleSeatTrace::ReadValue(rideOff + 0xB0, animation) || !animation ||
        !VehicleSeatTrace::ReadValue(animation, vtable) || !vtable ||
        !VehicleSeatTrace::ReadValue(vtable + 0x20, target) || !target ||
        !VehicleSeatTrace::ReadValue(
            vtable + kAnimationReadyOffset, readyTarget) || !readyTarget) {
        return false;
    }

    const uintptr_t slot = vtable + 0x20;
    const uintptr_t readySlot = vtable + kAnimationReadyOffset;
    const uintptr_t installed = g_animationSlot.load(std::memory_order_acquire);
    const uintptr_t installedReady =
        g_animationReadySlot.load(std::memory_order_acquire);
    if ((installed && installed != slot) ||
        (installedReady && installedReady != readySlot)) {
        return false;
    }
    g_activeAnimation.store(animation, std::memory_order_release);
    if (installed == slot && installedReady == readySlot)
        return true;

    g_originalAnimState = reinterpret_cast<AnimStateFn>(target);
    g_originalAnimReady = reinterpret_cast<AnimReadyFn>(readyTarget);
    if (!VtableLocator::SwapSlot(
            readySlot, readyTarget, reinterpret_cast<void*>(&HookAnimReady))) {
        return false;
    }
    if (!VtableLocator::SwapSlot(
            slot, target, reinterpret_cast<void*>(&HookAnimState))) {
        VtableLocator::SwapSlot(
            readySlot, reinterpret_cast<uintptr_t>(&HookAnimReady),
            reinterpret_cast<void*>(readyTarget));
        return false;
    }
    g_animationSlot.store(slot, std::memory_order_release);
    g_animationReadySlot.store(readySlot, std::memory_order_release);
    return true;
}

void TryRequestNativeExit(uintptr_t rideOff)
{
    uintptr_t plugin = 0;
    uint8_t current = 0;
    uint8_t next = 0;
    if (!VehicleSeatTrace::ReadValue(rideOff + 0x88, plugin) || !plugin ||
        !VehicleSeatTrace::ReadValue(plugin + 0x118, current) ||
        !VehicleSeatTrace::ReadValue(plugin + 0x11A, next) ||
        current != 3 || next != 3 ||
        !RideOffSession::MarkNativeExitRequested() ||
        !VehicleSeatTrace::WriteValue<uint8_t>(plugin + 0x11A, 0)) {
        return;
    }
    std::ostringstream oss;
    oss << "RideOff native Free-state exit requested"
        << " elapsedMs=" << GetTickCount64() -
            g_enterTick.load(std::memory_order_relaxed)
        << " current=" << static_cast<uint32_t>(current)
        << " next=" << static_cast<uint32_t>(next) << "->0";
    g_logger->Log(oss.str());
}

int64_t __fastcall HookRideOffEnter(
    uintptr_t rideOff, uintptr_t a2, uintptr_t a3)
{
    g_enterTick.store(GetTickCount64(), std::memory_order_relaxed);
    const bool animObserverInstalled = TryInstallAnimObserver(rideOff);
    const int64_t result = g_originalEnter(rideOff, a2, a3);
    RideOffSession::Begin(rideOff);
    std::ostringstream oss;
    oss << "RideOff Enter vtable original result=" << result
        << " rideOff=" << VehicleSeatTrace::Hex(rideOff)
        << " animObserver=" << (animObserverInstalled ? 1 : 0)
        << " session started";
    AppendSnapshot(oss, rideOff);
    g_logger->Log(oss.str());
    return result;
}

char __fastcall HookRideOffUpdate(uintptr_t rideOff, float delta)
{
    const uintptr_t previous = RideOffSession::EnterUpdate(rideOff);
    const char result = g_originalUpdate(rideOff, delta);
    RideOffSession::LeaveUpdate(previous);
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
    uintptr_t runtime = 0;
    uint32_t runtimeMode = 0;
    if (RideOffSession::MarkFinalizerForced() &&
        VehicleSeatTrace::ReadValue(rideOff + 0x190, runtime) && runtime) {
        VehicleSeatTrace::ReadValue(runtime + 0x2A0, runtimeMode);
        RideOffFinalizer::Force(runtime, runtimeMode == 3);
    }
    TryRequestNativeExit(rideOff);
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
    if (!RideOffFinalizer::TryInstall(gameModule, logger)) {
        logger.Log("RideOff native finalizer lookup failed");
        return false;
    }

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
    logger.Log("RideOff Enter/Update/RunPresentation observers installed");
    return true;
}

} // namespace RideOffVtableTrace
