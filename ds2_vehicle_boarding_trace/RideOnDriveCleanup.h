#pragma once

#include <cstdint>

namespace RideOnDriveCleanup {

struct Result {
    bool resetAnim = false;
    bool clearedPose = false;
    uint32_t animStateBefore = 0;
    uint32_t animStateAfter = 0;
    uint8_t poseActiveBefore = 0;
    uint8_t poseActiveAfter = 0;
    uint16_t poseId = 0;
};

bool ReadAnimState(uintptr_t rideOn, uint32_t& state);
void Apply(uintptr_t rideOn, Result& result);

} // namespace RideOnDriveCleanup
