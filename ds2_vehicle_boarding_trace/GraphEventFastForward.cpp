#include "pch.h"
#include "GraphEventFastForward.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

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

bool __fastcall HookBoolEvent(
    uintptr_t manager, uint32_t mappedEventId, int32_t contextIndex)
{
    const bool nativeResult =
        g_original(manager, mappedEventId, contextIndex);
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
