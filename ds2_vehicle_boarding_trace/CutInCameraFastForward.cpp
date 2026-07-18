#include "pch.h"
#include "CutInCameraFastForward.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <array>
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
constexpr std::array<uint32_t, 16> kBoardingActionHashes = {
    0x75B4F600, 0x3897A3D5, 0x3DFD6EFC, 0x01DB16B4,
    0x6F53F3A5, 0x53758BED, 0x11A19E23, 0x4665CE53,
    0x7A43B61B, 0x5A9F4628, 0x0D5B1658, 0x317D6E10,
    0x1AC30DFF, 0x5D599AE0, 0x0A9DCA90, 0x36BBB2D8};

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

bool IsBoardingHash(uint32_t hash)
{
    for (const uint32_t candidate : kBoardingActionHashes) {
        if (candidate == hash)
            return true;
    }
    return false;
}

bool CanFastForward(const PlaybackState& state)
{
    return state.active && !state.finished && !state.controllerActive &&
        !state.switchPending && state.variant &&
        IsBoardingHash(state.actionHash) &&
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
    if (!haveBefore || !ReadState(camera, state) ||
        !FastBoardingSession::ShouldFastForwardCutIn(camera) ||
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
    if (!FastBoardingSession::MarkCutInFastForwarded(
            camera, state.actionHash)) {
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
    const bool relevant = ReadState(camera, before) && before.finished &&
        IsBoardingHash(before.actionHash) &&
        FastBoardingSession::IsActiveCutInSession(
            camera, before.actionHash);
    g_originalDeactivate(camera);
    if (!relevant)
        return;
    PlaybackState after = {};
    const bool clean = ReadState(camera, after) && !after.active &&
        !after.finished && !after.variant &&
        after.actionHash == UINT32_MAX &&
        !after.flags && !after.switchPending;
    std::ostringstream oss;
    oss << "FastBoarding CutIn Deactivate clean=" << (clean ? 1 : 0)
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
