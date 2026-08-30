#include "pch.h"
#include "MoverRootMotionObserver.h"

#include "FastBoardingSession.h"
#include "RideOffMoverSnapshot.h"
#include "RideOffQueueClock.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>

namespace MoverRootMotionObserver {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSPlayerMoverAccessor@@";
constexpr uint32_t kModifyAnimatedPoseSlotIndex = 1;
constexpr char kPhysicsUpdateSignature[] =
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 "
    "48 81 EC 80 00 00 00 C5 F8 29 74 24 ? 48 8D B1";
constexpr float kGroundProbeDistance = 5.0f;
constexpr size_t kMaximumRecordedPhysicsCalls = 32;

using ModifyAnimatedPoseFn = void(__fastcall*)(
    uintptr_t self, float frameDelta, uintptr_t poseWrapper);
using PhysicsUpdateMovementFn = uint8_t(__fastcall*)(
    uintptr_t self, float frameDelta, uint32_t updateFlags);

struct MotionVector {
    float x;
    float y;
    float z;
    float w;
};

struct PhysicsCall {
    uintptr_t proxy = 0;
    float frameDelta = 0.0f;
    uint32_t updateFlags = 0;
};

std::atomic<bool> g_started{false};
std::atomic<uint32_t> g_groundAttemptSession{0};
const Logger* g_logger = nullptr;
ModifyAnimatedPoseFn g_original = nullptr;
PhysicsUpdateMovementFn g_originalPhysicsUpdate = nullptr;
thread_local uint32_t t_physicsSession = 0;
thread_local size_t t_physicsCallCount = 0;
thread_local size_t t_nextPhysicsCall = 0;
thread_local PhysicsCall t_physicsCalls[kMaximumRecordedPhysicsCalls] = {};

uint8_t __fastcall HookPhysicsUpdateMovement(
    uintptr_t self, float frameDelta, uint32_t updateFlags)
{
    const uint8_t result =
        g_originalPhysicsUpdate(self, frameDelta, updateFlags);
    const uint32_t session = RideOffSession::ActiveId();
    if (session) {
        if (t_physicsSession != session) {
            t_physicsSession = session;
            t_physicsCallCount = 0;
            t_nextPhysicsCall = 0;
        }
        t_physicsCalls[t_nextPhysicsCall] = {
            self, frameDelta, updateFlags};
        t_nextPhysicsCall =
            (t_nextPhysicsCall + 1) % kMaximumRecordedPhysicsCalls;
        if (t_physicsCallCount < kMaximumRecordedPhysicsCalls)
            ++t_physicsCallCount;
    }
    return result;
}

bool FindPhysicsCall(uintptr_t proxy, PhysicsCall& call)
{
    for (size_t i = 0; i < t_physicsCallCount; ++i) {
        const size_t index =
            (t_nextPhysicsCall + kMaximumRecordedPhysicsCalls - 1 - i) %
            kMaximumRecordedPhysicsCalls;
        if (t_physicsCalls[index].proxy == proxy) {
            call = t_physicsCalls[index];
            return true;
        }
    }
    return false;
}

bool CommitEntityPosition(
    uintptr_t entity, const double (&position)[3])
{
    if (!entity || !std::isfinite(position[0]) ||
        !std::isfinite(position[1]) || !std::isfinite(position[2])) {
        return false;
    }
    auto* transformLock = reinterpret_cast<CRITICAL_SECTION*>(
        entity + 0x2A8);
    if (!TryEnterCriticalSection(transformLock))
        EnterCriticalSection(transformLock);
    const bool written =
        VehicleSeatTrace::WriteValue(entity + 0xE8, position[0]) &&
        VehicleSeatTrace::WriteValue(entity + 0xF0, position[1]) &&
        VehicleSeatTrace::WriteValue(entity + 0xF8, position[2]);
    if (written) {
        InterlockedOr64(
            reinterpret_cast<volatile LONG64*>(entity + 0x98), 1);
    }
    LeaveCriticalSection(transformLock);
    return written;
}

bool GroundTerminalPose(
    const RideOffMoverSnapshot::Snapshot& airborne,
    RideOffMoverSnapshot::Snapshot& grounded,
    const char*& failure)
{
    failure = "unknown";
    PhysicsCall call = {};
    if (!airborne.proxyPositionValid || !airborne.physicsProxy) {
        failure = "proxy-position-unavailable";
        return false;
    }
    if (!FindPhysicsCall(airborne.physicsProxy, call)) {
        failure = "proxy-call-unavailable";
        return false;
    }
    if (!std::isfinite(call.frameDelta) || call.frameDelta <= 0.0f ||
        call.frameDelta > 1.0f) {
        failure = "invalid-frame-delta";
        return false;
    }

    const MotionVector downward = {
        0.0f, 0.0f, -kGroundProbeDistance / call.frameDelta, 0.0f};
    if (!VehicleSeatTrace::WriteValue(
            airborne.physicsProxy + 0x170, downward)) {
        failure = "downward-input-write";
        return false;
    }
    g_originalPhysicsUpdate(
        airborne.physicsProxy, call.frameDelta, call.updateFlags);
    const MotionVector stopped = {};
    if (!VehicleSeatTrace::WriteValue(
            airborne.physicsProxy + 0x170, stopped)) {
        failure = "stop-input-write";
        return false;
    }
    g_originalPhysicsUpdate(
        airborne.physicsProxy, call.frameDelta, call.updateFlags);

    double settledPosition[3] = {};
    const bool positionValid =
        VehicleSeatTrace::ReadValue(
            airborne.physicsProxy + 0x110, settledPosition[0]) &&
        VehicleSeatTrace::ReadValue(
            airborne.physicsProxy + 0x118, settledPosition[1]) &&
        VehicleSeatTrace::ReadValue(
            airborne.physicsProxy + 0x120, settledPosition[2]);
    const double downwardDistance = positionValid ?
        airborne.proxyPosition[2] - settledPosition[2] : 0.0;
    const bool collisionClipped = downwardDistance > 0.0 &&
        downwardDistance < kGroundProbeDistance - 0.05;
    if (!positionValid) {
        failure = "settled-position-read";
        return false;
    }
    if (!collisionClipped) {
        failure = "ground-probe-not-clipped";
        return false;
    }
    if (!CommitEntityPosition(airborne.entity, settledPosition)) {
        failure = "entity-position-commit";
        return false;
    }
    if (!RideOffMoverSnapshot::Capture(airborne.accessor, grounded)) {
        failure = "grounded-snapshot";
        return false;
    }
    failure = "none";
    return true;
}

void __fastcall HookModifyAnimatedPose(
    uintptr_t self, float frameDelta, uintptr_t poseWrapper)
{
    uintptr_t player = 0;
    const uintptr_t rideOn = FastBoardingSession::ActiveRideOn();
    if (rideOn)
        VehicleSeatTrace::ReadValue(rideOn + 0x98, player);
    const uint32_t session = RideOffSession::ActiveId();
    const bool terminalRideOffPose = session &&
        RideOffSession::GraphEndpointClaimed() &&
        RideOffQueueClock::IsSynchronized(session) &&
        !RideOffSession::CompletionReady() &&
        RideOffSession::MatchesMoverAccessor(self);
    RideOffMoverSnapshot::Snapshot beforeMover = {};
    if (terminalRideOffPose)
        RideOffMoverSnapshot::Capture(self, beforeMover);
    g_original(self, frameDelta, poseWrapper);
    RideOffMoverSnapshot::Snapshot afterMover = {};
    if (terminalRideOffPose)
        RideOffMoverSnapshot::Capture(self, afterMover);
    if (FastBoardingSession::ObservePostDrivePoseApplied(player)) {
        g_logger->Log("FastBoarding post-Drive player pose committed");
    }
    const bool firstAttempt = terminalRideOffPose &&
        g_groundAttemptSession.exchange(
            session, std::memory_order_acq_rel) != session;
    RideOffMoverSnapshot::Snapshot groundedMover = {};
    const char* groundingFailure = "not-attempted";
    const bool grounded = firstAttempt &&
        GroundTerminalPose(
            afterMover, groundedMover, groundingFailure);
    if (grounded && RideOffSession::MarkPostGraphPoseConsumed(self)) {
        std::ostringstream oss;
        oss << "FastRideOff terminal pose consumed and grounded; awaiting native completion"
            << " session=" << session
            << " elapsedMs=" << RideOffSession::ElapsedMs()
            << " accessor=" << VehicleSeatTrace::Hex(self)
            << " {" << RideOffMoverSnapshot::Format(
                "beforeMover", beforeMover) << "}"
            << " {" << RideOffMoverSnapshot::Format(
                "afterMover", afterMover) << "}"
            << " {" << RideOffMoverSnapshot::Format(
                "groundedMover", groundedMover) << "}";
        g_logger->Log(oss.str());
    } else if (firstAttempt) {
        g_logger->Log(
            "FastRideOff terminal pose grounding failed session=" +
            std::to_string(session) + " reason=" + groundingFailure);
    }
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;

    VtableLocator::Match match = {};
    VtableLocator::Match physicsMatch = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0,
            kModifyAnimatedPoseSlotIndex, match)) {
        logger.Log("DSPlayerMoverAccessor ModifyAnimatedPose lookup failed");
        return false;
    }
    const bool physicsUnique = VtableLocator::FindUnique(
        gameModule, kPhysicsUpdateSignature, physicsMatch);
    if (!physicsUnique &&
        !(physicsMatch.target && physicsMatch.rttiMatches == 1)) {
        logger.Log("PhysicsCharacterMoverProxy UpdateMovement lookup failed");
        return false;
    }
    g_logger = &logger;
    g_original = reinterpret_cast<ModifyAnimatedPoseFn>(match.target);
    g_originalPhysicsUpdate = reinterpret_cast<PhysicsUpdateMovementFn>(
        physicsMatch.target);
    if (!VtableLocator::SwapSlot(
            physicsMatch.slot, physicsMatch.target,
            reinterpret_cast<void*>(&HookPhysicsUpdateMovement))) {
        logger.Log("PhysicsCharacterMoverProxy UpdateMovement install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookModifyAnimatedPose))) {
        VtableLocator::SwapSlot(
            physicsMatch.slot,
            reinterpret_cast<uintptr_t>(&HookPhysicsUpdateMovement),
            reinterpret_cast<void*>(physicsMatch.target));
        logger.Log("DSPlayerMoverAccessor ModifyAnimatedPose install failed");
        return false;
    }

    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kPoseComponent);
    RideOffSession::ReportComponentReady(RideOffSession::kPoseComponent);
    g_started.store(true, std::memory_order_release);
    logger.Log("FastBoarding DSPlayerMoverAccessor ModifyAnimatedPose wrapper installed");
    logger.Log("FastRideOff terminal proxy observer installed");
    return true;
}

} // namespace MoverRootMotionObserver
