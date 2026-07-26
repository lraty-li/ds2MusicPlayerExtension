#include "pch.h"
#include "CutInCameraFastForward.h"

#include "FastBoardingSession.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

namespace CutInCameraFastForward {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSCutInCamera@@";
constexpr uint32_t kDeactivateSlotIndex = 5;
constexpr uint32_t kPlaybackSlotIndex = 9;
constexpr uint32_t kHoldLastFrameFlag = 0x00002000;
constexpr uint32_t kAdvanceVariantFlag = 0x01000000;
constexpr uint32_t kUnsupportedFlags =
    kHoldLastFrameFlag | kAdvanceVariantFlag;
constexpr uint32_t kMaximumExtraUpdates = 768;

using PlaybackFn = void(__fastcall*)(uintptr_t camera, float frameDelta);
using DeactivateFn = void(__fastcall*)(uintptr_t camera);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
PlaybackFn g_original = nullptr;
DeactivateFn g_originalDeactivate = nullptr;

struct PlaybackState {
    uintptr_t variant = 0;
    uint32_t actionHash = 0;
    uint32_t flags = 0;
    int32_t variantIndex = -1;
    float elapsed = 0.0f;
    float duration = 0.0f;
    uint8_t active = 0;
    uint8_t finished = 0;
    uint8_t firstPostUpdatePending = 0;
    uint8_t controllerActive = 0;
    uint8_t switchPending = 0;
};

bool ReadState(uintptr_t camera, PlaybackState& state)
{
    return VehicleSeatTrace::ReadValue(camera + 0x3C, state.actionHash) &&
        VehicleSeatTrace::ReadValue(camera + 0x40, state.flags) &&
        VehicleSeatTrace::ReadValue(camera + 0x44, state.variantIndex) &&
        VehicleSeatTrace::ReadValue(camera + 0x58, state.variant) &&
        VehicleSeatTrace::ReadValue(camera + 0x190, state.elapsed) &&
        VehicleSeatTrace::ReadValue(camera + 0x194, state.duration) &&
        VehicleSeatTrace::ReadValue(camera + 0x250, state.active) &&
        VehicleSeatTrace::ReadValue(camera + 0x251, state.finished) &&
        VehicleSeatTrace::ReadValue(
            camera + 0x252, state.firstPostUpdatePending) &&
        VehicleSeatTrace::ReadValue(camera + 0x257, state.controllerActive) &&
        VehicleSeatTrace::ReadValue(camera + 0x258, state.switchPending);
}

bool CanFastForward(const PlaybackState& state)
{
    return state.active && !state.finished && !state.controllerActive &&
        !state.switchPending && state.variant &&
        !(state.flags & kUnsupportedFlags) &&
        state.duration > state.elapsed && state.duration < 30.0f;
}

void LogProgress(
    const PlaybackState& before, const PlaybackState& state,
    uint32_t calls)
{
    std::ostringstream oss;
    oss << "FastBoarding CutIn playback advanced"
        << " hash=0x" << std::hex << state.actionHash << std::dec
        << " flags=0x" << std::hex << state.flags << std::dec
        << " variant=" << state.variantIndex
        << " calls=" << calls
        << " elapsed=" << before.elapsed << "->" << state.elapsed
        << " duration=" << state.duration
        << " finished=" << static_cast<uint32_t>(state.finished);
    g_logger->Log(oss.str());
}

void __fastcall HookPlayback(uintptr_t camera, float frameDelta)
{
    PlaybackState before = {};
    const bool haveBefore = ReadState(camera, before);
    g_original(camera, frameDelta);

    PlaybackState state = {};
    const bool fastBoarding =
        FastBoardingSession::ShouldFastForwardCutIn(camera);
    const bool fastRideOff = !fastBoarding &&
        RideOffSession::ShouldFastForwardCutIn(before.actionHash);
    if (!haveBefore || !ReadState(camera, state) ||
        (!fastBoarding && !fastRideOff) ||
        before.firstPostUpdatePending ||
        state.firstPostUpdatePending ||
        !CanFastForward(state) || state.variant != before.variant ||
        state.actionHash != before.actionHash ||
        state.elapsed <= before.elapsed) {
        return;
    }

    const float step = state.elapsed - before.elapsed;
    const float expectedDuration = state.duration;
    if (!std::isfinite(step) || !std::isfinite(state.elapsed) ||
        !std::isfinite(state.duration) || step < 0.00001f) {
        return;
    }
    double wantedValue =
        static_cast<double>(state.duration - state.elapsed) / step + 2.0;
    if (wantedValue < 1.0)
        wantedValue = 1.0;
    if (wantedValue > kMaximumExtraUpdates)
        wantedValue = kMaximumExtraUpdates;
    const uint32_t wanted = static_cast<uint32_t>(wantedValue);
    const bool claimed = fastBoarding ?
        FastBoardingSession::MarkCutInFastForwarded(camera, state.actionHash) :
        RideOffSession::MarkCutInFastForwarded(state.actionHash);
    if (!claimed) {
        return;
    }

    uint32_t extraCalls = 0;
    while (extraCalls < wanted) {
        const float previousElapsed = state.elapsed;
        g_original(camera, frameDelta);
        ++extraCalls;
        PlaybackState next = {};
        if (!ReadState(camera, next))
            break;
        state = next;
        if (state.finished)
            break;
        if (!CanFastForward(state) || state.variant != before.variant ||
            state.actionHash != before.actionHash ||
            state.duration != expectedDuration ||
            state.elapsed <= previousElapsed) {
            break;
        }
    }
    LogProgress(before, state, extraCalls + 1);
}

void __fastcall HookDeactivate(uintptr_t camera)
{
    PlaybackState before = {};
    const bool haveBefore = ReadState(camera, before);
    const bool relevant = haveBefore && before.finished &&
        FastBoardingSession::IsActiveCutInSession(
            camera, before.actionHash);
    const bool rideOff = haveBefore && before.finished &&
        RideOffSession::IsActiveCutInAction(before.actionHash);
    g_originalDeactivate(camera);
    if (!relevant && !rideOff)
        return;
    PlaybackState after = {};
    const bool clean = ReadState(camera, after) && !after.active &&
        !after.finished && !after.variant &&
        after.actionHash == UINT32_MAX &&
        !after.flags && !after.switchPending;
    std::ostringstream oss;
    oss << (rideOff ? "RideOff" : "FastBoarding")
        << " CutIn Deactivate clean=" << (clean ? 1 : 0)
        << " hash=0x" << std::hex << before.actionHash
        << " flags=0x" << before.flags << std::dec
        << " variant=" << before.variantIndex;
    g_logger->Log(oss.str());
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load())
        return true;

    VtableLocator::Match match = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0,
            kPlaybackSlotIndex, match)) {
        logger.Log("DSCutInCamera RTTI/vtable lookup failed matches=" +
            std::to_string(match.rttiMatches));
        return false;
    }

    std::ostringstream candidate;
    candidate << "DSCutInCamera playback candidate"
        << " target=" << VehicleSeatTrace::Hex(match.target)
        << " vtable=" << VehicleSeatTrace::Hex(match.vtable)
        << " slot=" << match.slotIndex
        << " col=" << VehicleSeatTrace::Hex(match.col)
        << " type=" << match.typeName;
    logger.Log(candidate.str());

    g_logger = &logger;
    g_original = reinterpret_cast<PlaybackFn>(match.target);
    const uintptr_t deactivateSlot = match.vtable +
        kDeactivateSlotIndex * sizeof(uintptr_t);
    uintptr_t deactivateTarget = 0;
    if (!VehicleSeatTrace::ReadValue(
            deactivateSlot, deactivateTarget) || !deactivateTarget) {
        logger.Log("DSCutInCamera Deactivate target read failed");
        return false;
    }
    g_originalDeactivate =
        reinterpret_cast<DeactivateFn>(deactivateTarget);
    if (!VtableLocator::SwapSlot(
            deactivateSlot, deactivateTarget,
            reinterpret_cast<void*>(&HookDeactivate))) {
        logger.Log("DSCutInCamera Deactivate vtable install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookPlayback))) {
        VtableLocator::SwapSlot(
            deactivateSlot,
            reinterpret_cast<uintptr_t>(&HookDeactivate),
            reinterpret_cast<void*>(deactivateTarget));
        logger.Log("DSCutInCamera playback vtable install failed");
        return false;
    }
    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kCutInComponent);
    g_started.store(true);
    logger.Log("FastBoarding DSCutInCamera playback wrapper installed");
    return true;
}

} // namespace CutInCameraFastForward
