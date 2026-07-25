#pragma once

#include <cstdint>

namespace RideOffSession {

void Begin(uintptr_t rideOff);
void ObserveCutInAction(uintptr_t rideOff, uint32_t actionHash);
bool ShouldFastForwardCutIn(uint32_t actionHash);
bool MarkCutInFastForwarded(uint32_t actionHash);
bool MarkFinalizerForced();
uintptr_t EnterUpdate(uintptr_t rideOff);
void LeaveUpdate(uintptr_t previousRideOff);
uint32_t CurrentId();

} // namespace RideOffSession
