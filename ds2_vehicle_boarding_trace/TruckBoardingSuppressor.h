#pragma once

#include "Logger.h"

#include <cstdint>

namespace TruckBoardingSuppressor {

bool TrySuppress(uintptr_t truck, const Logger& logger);

} // namespace TruckBoardingSuppressor
