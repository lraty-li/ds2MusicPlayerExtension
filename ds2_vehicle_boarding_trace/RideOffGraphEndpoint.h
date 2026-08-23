#pragma once

#include "Logger.h"

#include <cstddef>
#include <cstdint>

namespace RideOffGraphEndpoint {

uintptr_t FindCallerReturn(
    uintptr_t textStart, size_t textSize, uintptr_t evaluatorSlot);
void Enable(uintptr_t callerReturn, const Logger& logger);
void Prepare(
    const Logger& logger, uintptr_t caller, uintptr_t output,
    uintptr_t descriptor, uint8_t mode, float timeScale,
    uint8_t evaluatePose);
void ObserveResult(
    const Logger& logger, uintptr_t caller, uintptr_t output,
    uintptr_t descriptor);
bool HasPendingQueueClockAdvance();
bool TakePendingQueueClockAdvance(
    uint32_t& session, float& extraSeconds);

} // namespace RideOffGraphEndpoint
