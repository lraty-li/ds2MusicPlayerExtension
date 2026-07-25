#include "pch.h"
#include "TruckSeatTransitionObserver.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace TruckSeatTransitionObserver {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSVehicleTruck@@";
constexpr uint32_t kUpdateSlot34 = 34;
constexpr uint32_t kUpdateSlot35 = 35;

using TruckUpdateFn = void(__fastcall*)(uintptr_t truck, float frameDelta);

struct SeatSnapshot {
    uintptr_t controller = 0;
    uintptr_t controllerPlayback = 0;
    int32_t currentState = 0;
    int32_t requestedState = 0;
    uint8_t controllerPlaybackState = 0;
};

std::atomic<bool> g_started{false};
std::atomic<uintptr_t> g_vtable{0};
std::atomic<uintptr_t> g_boundTruck{0};
std::atomic<uint32_t> g_boundSession{0};
std::atomic<uint32_t> g_suppressedSession{0};
const Logger* g_logger = nullptr;
TruckUpdateFn g_original34 = nullptr;
TruckUpdateFn g_original35 = nullptr;

void __fastcall HookSlot34(uintptr_t truck, float frameDelta);
void __fastcall HookSlot35(uintptr_t truck, float frameDelta);

bool ReadSnapshot(uintptr_t truck, SeatSnapshot& state)
{
    if (!VehicleSeatTrace::ReadValue(truck + 0x12F8, state.controller) ||
        !VehicleSeatTrace::ReadValue(truck + 0x1310, state.currentState) ||
        !VehicleSeatTrace::ReadValue(truck + 0x1314, state.requestedState)) {
        return false;
    }
    if (state.controller &&
        !VehicleSeatTrace::ReadValue(
            state.controller + 0x50, state.controllerPlayback)) {
        return false;
    }
    if (state.controllerPlayback &&
        !VehicleSeatTrace::ReadValue(
            state.controllerPlayback + 0x18,
            state.controllerPlaybackState)) {
        return false;
    }
    return true;
}

bool CompareExchangeRequest(
    uintptr_t address, int32_t expected, int32_t desired)
{
    __try {
        return InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(address),
            static_cast<LONG>(desired), static_cast<LONG>(expected)) ==
            expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsBoardingRequest(int32_t state)
{
    return state >= 3 && state <= 6;
}

bool TryCancelBoardingRequest(uintptr_t truck, const SeatSnapshot& state)
{
    if (!FastBoardingSession::AllComponentsReady() ||
        state.currentState != 0 ||
        !IsBoardingRequest(state.requestedState) ||
        !state.controller || !state.controllerPlayback ||
        state.controllerPlaybackState != 2) {
        return false;
    }

    if (!CompareExchangeRequest(
            truck + 0x1314, state.requestedState, -1))
        return false;

    const uint32_t session = FastBoardingSession::CurrentSessionId();
    if (g_suppressedSession.exchange(
            session, std::memory_order_acq_rel) != session) {
        std::ostringstream oss;
        oss << "FastBoarding TruckSeat boarding request suppressed"
            << " session=" << session
            << " truck=" << VehicleSeatTrace::Hex(truck)
            << " current=" << state.currentState
            << " request=" << state.requestedState
            << " playbackState="
            << static_cast<uint32_t>(state.controllerPlaybackState);
        g_logger->Log(oss.str());
    }
    return true;
}

bool IsBoundTruck(uintptr_t truck)
{
    const uint32_t session = g_boundSession.load(std::memory_order_acquire);
    return truck && truck == g_boundTruck.load(std::memory_order_acquire) &&
        session && session == FastBoardingSession::CurrentSessionId() &&
        FastBoardingSession::ActiveRideOn() != 0;
}

bool DiscreteChanged(
    const SeatSnapshot& before, const SeatSnapshot& after)
{
    return before.controller != after.controller ||
        before.controllerPlayback != after.controllerPlayback ||
        before.currentState != after.currentState ||
        before.requestedState != after.requestedState ||
        before.controllerPlaybackState != after.controllerPlaybackState;
}

void LogStateChange(
    uint32_t slot, const SeatSnapshot& before,
    const SeatSnapshot& after)
{
    std::ostringstream oss;
    oss << "TruckSeat slot=" << slot << " state="
        << before.currentState << "->" << before.requestedState
        << " => " << after.currentState << "->"
        << after.requestedState << " playbackState="
        << static_cast<uint32_t>(before.controllerPlaybackState)
        << "->" << static_cast<uint32_t>(after.controllerPlaybackState);
    g_logger->Log(oss.str());
}

void ObserveUpdate(
    uint32_t slot, TruckUpdateFn original,
    uintptr_t truck, float frameDelta)
{
    SeatSnapshot before = {};
    const bool relevant = IsBoundTruck(truck) && ReadSnapshot(truck, before);
    if (relevant && TryCancelBoardingRequest(truck, before))
        ReadSnapshot(truck, before);
    original(truck, frameDelta);
    if (!relevant)
        return;

    SeatSnapshot after = {};
    if (!IsBoundTruck(truck) || !ReadSnapshot(truck, after))
        return;
    if (DiscreteChanged(before, after))
        LogStateChange(slot, before, after);
}

void __fastcall HookSlot34(uintptr_t truck, float frameDelta)
{
    ObserveUpdate(kUpdateSlot34, g_original34, truck, frameDelta);
}

void __fastcall HookSlot35(uintptr_t truck, float frameDelta)
{
    ObserveUpdate(kUpdateSlot35, g_original35, truck, frameDelta);
}

bool TryInstallCompatibleVtable(uintptr_t vtable)
{
    uintptr_t target34 = 0;
    uintptr_t target35 = 0;
    const uintptr_t slot34 = vtable + kUpdateSlot34 * sizeof(uintptr_t);
    const uintptr_t slot35 = vtable + kUpdateSlot35 * sizeof(uintptr_t);
    if (!VehicleSeatTrace::ReadValue(slot34, target34) ||
        !VehicleSeatTrace::ReadValue(slot35, target35)) {
        return false;
    }
    if (target34 == reinterpret_cast<uintptr_t>(&HookSlot34) &&
        target35 == reinterpret_cast<uintptr_t>(&HookSlot35)) {
        return true;
    }
    if (target34 != reinterpret_cast<uintptr_t>(g_original34) ||
        target35 != reinterpret_cast<uintptr_t>(g_original35)) {
        return false;
    }
    if (!VtableLocator::SwapSlot(
            slot34, target34, reinterpret_cast<void*>(&HookSlot34))) {
        return false;
    }
    if (VtableLocator::SwapSlot(
            slot35, target35, reinterpret_cast<void*>(&HookSlot35))) {
        return true;
    }
    VtableLocator::SwapSlot(
        slot34, reinterpret_cast<uintptr_t>(&HookSlot34),
        reinterpret_cast<void*>(target34));
    return false;
}

} // namespace

bool PrepareProcessAttach(uintptr_t rideOn)
{
    if (!g_started.load(std::memory_order_acquire) ||
        FastBoardingSession::ActiveRideOn() != rideOn) {
        return false;
    }
    uintptr_t player = 0;
    uintptr_t truck = 0;
    uintptr_t vfptr = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0x98, player) || !player ||
        !VehicleSeatTrace::ReadValue(player + 0x80, truck) || !truck ||
        !VehicleSeatTrace::ReadValue(truck, vfptr))
        return false;
    if (vfptr != g_vtable.load(std::memory_order_acquire) &&
        !TryInstallCompatibleVtable(vfptr)) {
        return true;
    }

    const uint32_t session = FastBoardingSession::CurrentSessionId();
    if (g_boundTruck.load(std::memory_order_relaxed) == truck &&
        g_boundSession.load(std::memory_order_relaxed) == session) {
        SeatSnapshot state = {};
        if (!ReadSnapshot(truck, state))
            return false;
        TryCancelBoardingRequest(truck, state);
        if (!ReadSnapshot(truck, state))
            return false;
        return !IsBoardingRequest(state.currentState) &&
            !IsBoardingRequest(state.requestedState);
    }
    g_boundTruck.store(truck, std::memory_order_relaxed);
    g_boundSession.store(session, std::memory_order_release);

    SeatSnapshot state = {};
    if (!ReadSnapshot(truck, state))
        return false;
    TryCancelBoardingRequest(truck, state);
    if (!ReadSnapshot(truck, state))
        return false;
    return !IsBoardingRequest(state.currentState) &&
        !IsBoardingRequest(state.requestedState);
}

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;

    VtableLocator::Match slot34 = {};
    VtableLocator::Match slot35 = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0, kUpdateSlot34, slot34) ||
        !VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0, kUpdateSlot35, slot35) ||
        slot34.vtable != slot35.vtable) {
        logger.Log("DSVehicleTruck seat observer RTTI lookup failed");
        return false;
    }

    g_logger = &logger;
    g_vtable.store(slot34.vtable, std::memory_order_release);
    g_original34 = reinterpret_cast<TruckUpdateFn>(slot34.target);
    g_original35 = reinterpret_cast<TruckUpdateFn>(slot35.target);
    if (!VtableLocator::SwapSlot(
            slot34.slot, slot34.target,
            reinterpret_cast<void*>(&HookSlot34))) {
        logger.Log("DSVehicleTruck slot34 observer install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            slot35.slot, slot35.target,
            reinterpret_cast<void*>(&HookSlot35))) {
        VtableLocator::SwapSlot(
            slot34.slot, reinterpret_cast<uintptr_t>(&HookSlot34),
            reinterpret_cast<void*>(slot34.target));
        logger.Log("DSVehicleTruck slot35 observer install failed");
        return false;
    }
    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kTruckSeatComponent);
    g_started.store(true, std::memory_order_release);
    std::ostringstream oss;
    oss << "DSVehicleTruck seat observers installed vtable="
        << VehicleSeatTrace::Hex(slot34.vtable)
        << " slot34=" << VehicleSeatTrace::Hex(slot34.target)
        << " slot35=" << VehicleSeatTrace::Hex(slot35.target);
    logger.Log(oss.str());
    return true;
}

} // namespace TruckSeatTransitionObserver
