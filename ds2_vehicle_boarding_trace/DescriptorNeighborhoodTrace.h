#pragma once

#include "Logger.h"

#include <cstdint>

namespace DescriptorNeighborhoodTrace {

void Initialize(const Logger& logger, uintptr_t moduleBase);
bool Active();
void MarkBoarding(
    uint32_t session, uint64_t tick, uintptr_t syncState);
void Observe(
    uintptr_t caller, uintptr_t descriptor, uint8_t mode, float weight,
    uint32_t frame, uintptr_t single, float duration,
    float syncDuration);
void FlushIfReady(uint64_t tick);

} // namespace DescriptorNeighborhoodTrace
