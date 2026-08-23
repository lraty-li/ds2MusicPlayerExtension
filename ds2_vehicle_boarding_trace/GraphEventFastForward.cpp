#include "pch.h"
#include "GraphEventFastForward.h"

#include "FastBoardingSession.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>

namespace GraphEventFastForward {
namespace {

constexpr char kExpectedTypeName[] = ".?AVGraphAnimationManager@@";
constexpr uint32_t kBoolEventSlotIndex = 28;

using BoolEventFn = bool(__fastcall*)(
    uintptr_t manager, uint32_t mappedEventId, int32_t contextIndex);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
BoolEventFn g_original = nullptr;
SRWLOCK g_rideOffEventLock = SRWLOCK_INIT;
uint32_t g_rideOffEventSession = 0;
std::array<uint8_t, 256> g_rideOffEventResults{};

void ObserveRideOffEvent(
    uintptr_t manager, uint32_t eventId, int32_t contextIndex,
    bool nativeResult)
{
    const uint32_t session = RideOffSession::ActiveId();
    if (!session || contextIndex != 0 ||
        eventId >= g_rideOffEventResults.size() ||
        !RideOffSession::MatchesGraphManager(manager)) {
        return;
    }

    const uint8_t encoded = nativeResult ? 2 : 1;
    bool log = false;
    const char* reason = "edge";
    AcquireSRWLockExclusive(&g_rideOffEventLock);
    if (g_rideOffEventSession != session) {
        g_rideOffEventSession = session;
        g_rideOffEventResults.fill(0);
    }
    const uint8_t previous = g_rideOffEventResults[eventId];
    if (!previous && nativeResult) {
        log = true;
        reason = "initial-true";
    } else if (previous && previous != encoded) {
        log = true;
    }
    g_rideOffEventResults[eventId] = encoded;
    ReleaseSRWLockExclusive(&g_rideOffEventLock);

    if (log) {
        std::ostringstream oss;
        oss << "RideOff graph bool event session=" << session
            << " elapsedMs=" << RideOffSession::ElapsedMs()
            << " event=" << eventId
            << " result=" << (nativeResult ? 1 : 0)
            << " reason=" << reason;
        g_logger->Log(oss.str());
    }
}

bool __fastcall HookBoolEvent(
    uintptr_t manager, uint32_t mappedEventId, int32_t contextIndex)
{
    const bool nativeResult =
        g_original(manager, mappedEventId, contextIndex);
    ObserveRideOffEvent(
        manager, mappedEventId, contextIndex, nativeResult);
    if (contextIndex != 0 ||
        !FastBoardingSession::MatchesGraphEvent(manager, mappedEventId)) {
        return nativeResult;
    }
    if (!FastBoardingSession::CompletionLayersReady(nativeResult))
        return false;
    if (!FastBoardingSession::MarkGraphEventForced())
        return true;

    g_logger->Log(
        "FastBoarding emitted native RideOn completion event manager=" +
        VehicleSeatTrace::Hex(manager) + " event=" +
        std::to_string(mappedEventId));
    return true;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load())
        return true;

    VtableLocator::Match match = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0,
            kBoolEventSlotIndex, match)) {
        logger.Log("GraphAnimationManager RTTI/vtable lookup failed matches=" +
            std::to_string(match.rttiMatches));
        return false;
    }

    std::ostringstream candidate;
    candidate << "GraphAnimationManager bool-event candidate"
        << " target=" << VehicleSeatTrace::Hex(match.target)
        << " vtable=" << VehicleSeatTrace::Hex(match.vtable)
        << " slot=" << match.slotIndex
        << " col=" << VehicleSeatTrace::Hex(match.col)
        << " type=" << match.typeName;
    logger.Log(candidate.str());

    g_logger = &logger;
    g_original = reinterpret_cast<BoolEventFn>(match.target);
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookBoolEvent))) {
        logger.Log("GraphAnimationManager bool-event vtable install failed");
        return false;
    }
    logger.Log("FastBoarding GraphAnimationManager event wrapper installed");
    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kGraphComponent);
    g_started.store(true);
    return true;
}

} // namespace GraphEventFastForward
