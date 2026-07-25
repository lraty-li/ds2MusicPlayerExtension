#pragma once

#include "Logger.h"

#include <cstdint>

namespace RideOffDescriptorTrace {

void Observe(
    const Logger& logger, uint32_t session, uintptr_t fullGame,
    uintptr_t caller, uintptr_t descriptor, uint8_t mode, float timeScale,
    uint8_t evaluatePose, float duration, float syncDuration,
    uint8_t reachedEnd);

} // namespace RideOffDescriptorTrace
