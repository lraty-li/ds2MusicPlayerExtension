#pragma once

#include <cstdint>

namespace FastBoardingSession {

constexpr uint32_t kScopeComponent = 1u << 0;
constexpr uint32_t kGraphComponent = 1u << 1;
constexpr uint32_t kCutInComponent = 1u << 2;
constexpr uint32_t kAnimationComponent = 1u << 3;
constexpr uint32_t kPoseComponent = 1u << 4;

void ReportComponentReady(uint32_t component);
bool AllComponentsReady();
void Begin(uintptr_t rideOn);
void ObserveProcessAttach(uintptr_t rideOn);
void ObserveDriveEnter(uintptr_t driveState);
bool ObservePostDrivePoseApplied(uintptr_t player);
uintptr_t EnterRideOnUpdate(uintptr_t rideOn);
void LeaveRideOnUpdate(uintptr_t previousRideOn);
bool ObserveRideOnUpdateComplete(uintptr_t rideOn);
bool MatchesGraphEvent(uintptr_t manager, uint32_t eventId);
bool CompletionLayersReady(bool nativeResult);
bool CanFastForwardAnimation();
bool ShouldFastForwardCutIn(uintptr_t cutInCamera);
bool IsActiveCutInSession(uintptr_t cutInCamera, uint32_t actionHash);
bool MarkGraphEventForced();
bool MarkCutInFastForwarded(uintptr_t cutInCamera, uint32_t actionHash);
void ConfirmAnimationFastForwarded();
uint32_t CurrentSessionId();
uintptr_t ActiveRideOn();

} // namespace FastBoardingSession
