#include "pch.h"
#include "MoverRootMotionObserver.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cstdint>

namespace MoverRootMotionObserver {
namespace {

constexpr char kExpectedTypeName[] = ".?AVDSPlayerMoverAccessor@@";
constexpr uint32_t kModifyAnimatedPoseSlotIndex = 1;

using ModifyAnimatedPoseFn = void(__fastcall*)(
    uintptr_t self, float frameDelta, uintptr_t poseWrapper);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
ModifyAnimatedPoseFn g_original = nullptr;

void __fastcall HookModifyAnimatedPose(
    uintptr_t self, float frameDelta, uintptr_t poseWrapper)
{
    uintptr_t player = 0;
    const uintptr_t rideOn = FastBoardingSession::ActiveRideOn();
    if (rideOn)
        VehicleSeatTrace::ReadValue(rideOn + 0x98, player);

    g_original(self, frameDelta, poseWrapper);
    if (FastBoardingSession::ObservePostDrivePoseApplied(player)) {
        g_logger->Log("FastBoarding post-Drive player pose committed");
    }
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;

    VtableLocator::Match match = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0,
            kModifyAnimatedPoseSlotIndex, match)) {
        logger.Log("DSPlayerMoverAccessor ModifyAnimatedPose lookup failed");
        return false;
    }

    g_logger = &logger;
    g_original = reinterpret_cast<ModifyAnimatedPoseFn>(match.target);
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookModifyAnimatedPose))) {
        logger.Log("DSPlayerMoverAccessor ModifyAnimatedPose install failed");
        return false;
    }

    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kPoseComponent);
    g_started.store(true, std::memory_order_release);
    logger.Log("FastBoarding DSPlayerMoverAccessor ModifyAnimatedPose wrapper installed");
    return true;
}

} // namespace MoverRootMotionObserver
