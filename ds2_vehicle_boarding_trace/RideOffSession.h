#pragma once

#include <cstdint>

namespace RideOffSession {

enum Component : uint32_t {
    kPoseComponent = 1u << 0,
    kGraphEndpointComponent = 1u << 1,
    kNativeDetachComponent = 1u << 2,
    kQueueClockComponent = 1u << 3,
};

void ReportComponentReady(Component component);
uint32_t Begin(uintptr_t rideOff, uintptr_t player);
void ObserveCutInAction(uintptr_t rideOff, uint32_t actionHash);
bool ShouldFastForwardCutIn(uint32_t actionHash);
bool MarkCutInFastForwarded(uint32_t actionHash);
bool IsActiveCutInAction(uint32_t actionHash);
bool TryClaimGraphEndpoint(
    uintptr_t output, uintptr_t descriptor, uint32_t& session);
bool IsClaimedGraphEndpoint(
    uintptr_t output, uintptr_t descriptor, uint32_t& session);
void ReleaseGraphEndpointClaim(
    uintptr_t output, uintptr_t descriptor, uint32_t session);
bool MarkGraphEndpointComplete(
    uintptr_t output, uintptr_t descriptor, uint32_t session);
bool TryRequestNativeExitBeforePoseConsumption(
    uintptr_t moverAccessor, uintptr_t& plugin);
bool MarkPostGraphPoseConsumed(uintptr_t moverAccessor);
bool GraphEndpointClaimed();
bool GraphEndpointComplete();
bool CompletionReady();
bool MarkFinalizerGateOpened();
uint32_t ActiveId();
uintptr_t ActivePlayer();
bool MatchesMoverAccessor(uintptr_t moverAccessor);
bool MatchesGraphManager(uintptr_t manager);
uint64_t ElapsedMs();
uintptr_t EnterUpdate(uintptr_t rideOff);
void LeaveUpdate(uintptr_t previousRideOff);
uint32_t CurrentId();

} // namespace RideOffSession
