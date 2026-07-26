#pragma once

#include "Logger.h"

#include <cstdint>

namespace TruckRideOffSuppressor {

bool TrySuppress(uintptr_t truck, const Logger& logger);

} // namespace TruckRideOffSuppressor
