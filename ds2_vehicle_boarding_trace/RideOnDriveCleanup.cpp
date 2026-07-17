#include "pch.h"
#include "RideOnDriveCleanup.h"

#include "VehicleSnapshot.h"

namespace RideOnDriveCleanup {
namespace {

bool ResetAnimStateToDrive(uintptr_t rideOn, uint32_t& before, uint32_t& after)
{
    uintptr_t anim = 0;
    uintptr_t inner = 0;
    uintptr_t vtbl = 0;
    uintptr_t requestStateFn = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0xB0, anim) || !anim)
        return false;
    if (!VehicleSeatTrace::ReadValue(anim + 0x8, inner) || !inner)
        return false;
    if (!VehicleSeatTrace::ReadValue(inner + 0x2E0, before))
        before = 0;
    if (!VehicleSeatTrace::ReadValue(anim, vtbl) || !vtbl)
        return false;
    if (!VehicleSeatTrace::ReadValue(vtbl + 0x20, requestStateFn) || !requestStateFn)
        return false;

    __try {
        reinterpret_cast<void(__fastcall*)(uintptr_t, uint32_t)>(requestStateFn)(anim, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    if (!VehicleSeatTrace::ReadValue(inner + 0x2E0, after))
        after = before;
    return true;
}

bool ClearPoseRequest(
    uintptr_t rideOn, uint8_t& activeBefore, uint8_t& activeAfter, uint16_t& poseId)
{
    uintptr_t owner = 0;
    uintptr_t poseOwner = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0xA0, owner) || !owner)
        return false;
    if (!VehicleSeatTrace::ReadValue(owner + 0x7558, poseOwner) || !poseOwner)
        return false;

    VehicleSeatTrace::ReadValue(poseOwner + 0x788, poseId);
    VehicleSeatTrace::ReadValue(poseOwner + 0x2104, activeBefore);
    if (!VehicleSeatTrace::WriteValue<uint8_t>(poseOwner + 0x2104, 0))
        return false;

    uint32_t dirtyBits = 0;
    VehicleSeatTrace::ReadValue(poseOwner + 0x2154, dirtyBits);
    VehicleSeatTrace::WriteValue<uint32_t>(poseOwner + 0x2154, dirtyBits | 0x1000);
    VehicleSeatTrace::ReadValue(poseOwner + 0x2104, activeAfter);
    return true;
}

} // namespace

bool ReadAnimState(uintptr_t rideOn, uint32_t& state)
{
    uintptr_t anim = 0;
    uintptr_t inner = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0xB0, anim) || !anim)
        return false;
    if (!VehicleSeatTrace::ReadValue(anim + 0x8, inner) || !inner)
        return false;
    return VehicleSeatTrace::ReadValue(inner + 0x2E0, state);
}

void Apply(uintptr_t rideOn, Result& result)
{
    if (!rideOn)
        return;

    result.resetAnim = ResetAnimStateToDrive(
        rideOn, result.animStateBefore, result.animStateAfter);
    result.clearedPose = ClearPoseRequest(
        rideOn, result.poseActiveBefore, result.poseActiveAfter, result.poseId);
}

} // namespace RideOnDriveCleanup
