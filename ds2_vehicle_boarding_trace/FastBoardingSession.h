#pragma once

#include <cstdint>

namespace FastBoardingSession {

constexpr uint32_t kScopeComponent = 1u << 0;
constexpr uint32_t kGraphComponent = 1u << 1;
constexpr uint32_t kCutInComponent = 1u << 2;
constexpr uint32_t kAnimationComponent = 1u << 3;

void ReportComponentReady(uint32_t component);
bool AllComponentsReady();
void Begin(uintptr_t rideOn);
void ObserveProcessAttach(uintptr_t rideOn);
void ObserveDriveEnter(uintptr_t driveState);
uintptr_t EnterRideOnUpdate(uintptr_t rideOn);
void LeaveRideOnUpdate(uintptr_t previousRideOn);
bool MatchesGraphEvent(uintptr_t manager, uint32_t eventId);
bool CompletionLayersReady(bool nativeResult);
bool ShouldFastForwardAnimation();
bool ShouldFastForwardCutIn(uintptr_t cutInCamera);
bool IsActiveCutInSession(uintptr_t cutInCamera, uint32_t actionHash);
bool MarkGraphEventForced();
bool MarkAnimationFastForwarded();
bool MarkCutInFastForwarded(uintptr_t cutInCamera, uint32_t actionHash);
void ConfirmAnimationFastForwarded();
void ConfirmCutInFastForwarded(uintptr_t cutInCamera);
uintptr_t ActiveRideOn();

} // namespace FastBoardingSession
