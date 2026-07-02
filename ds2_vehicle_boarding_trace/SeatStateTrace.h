#pragma once

#include <windows.h>
#include "Logger.h"

namespace SeatStateTrace {
    void RememberSeatObject(uintptr_t rideOn, uintptr_t seatObject);
    bool TryInstall(HMODULE gameModule, const Logger& logger);
}
