#pragma once

#include "Logger.h"

#include <cstdint>

namespace RideOffQueueClock {

bool TryInstall(const Logger& logger);
bool IsSynchronized(uint32_t session);

} // namespace RideOffQueueClock
