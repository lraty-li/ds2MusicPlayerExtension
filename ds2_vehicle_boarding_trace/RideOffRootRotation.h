#pragma once

#include "Logger.h"

#include <cstdint>
#include <windows.h>

namespace RideOffRootRotation {

uintptr_t EnterRecoveryPose(uintptr_t moverAccessor);
void LeaveRecoveryPose(uintptr_t previousMover);
bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace RideOffRootRotation
