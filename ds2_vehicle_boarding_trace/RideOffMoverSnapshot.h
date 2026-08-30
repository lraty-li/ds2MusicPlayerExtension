#pragma once

#include "VehicleSnapshot.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace RideOffMoverSnapshot {

struct Snapshot {
    uintptr_t accessor = 0;
    uintptr_t mover = 0;
    uintptr_t entity = 0;
    uintptr_t model = 0;
    uintptr_t physicsProxy = 0;
    double position[3] = {};
    double proxyPosition[3] = {};
    float entityOffset[3] = {};
    float frameMove[3] = {};
    float rootMotion[3] = {};
    uint32_t animationState = UINT32_MAX;
    uint32_t requestedPhysicsMode = UINT32_MAX;
    uint32_t appliedPhysicsMode = UINT32_MAX;
    uint32_t rootMotionBuffer = UINT32_MAX;
    uint8_t physicsModePending = UINT8_MAX;
    bool positionValid = false;
    bool proxyPositionValid = false;
    bool entityOffsetValid = false;
    bool rootMotionValid = false;
};

inline void CapturePhysicsPosition(Snapshot& snapshot)
{
    uintptr_t owner = 0;
    uintptr_t holder = 0;
    uintptr_t selector = 0;
    if (!VehicleSeatTrace::ReadValue(
            snapshot.mover + 0x330, owner) || !owner ||
        !VehicleSeatTrace::ReadValue(owner + 0x330, holder) || !holder ||
        !VehicleSeatTrace::ReadValue(holder + 0x1E0, selector) || !selector ||
        !VehicleSeatTrace::ReadValue(
            selector + 0x40, snapshot.physicsProxy) ||
        !snapshot.physicsProxy) {
        return;
    }
    snapshot.proxyPositionValid =
        VehicleSeatTrace::ReadValue(
            snapshot.physicsProxy + 0x110, snapshot.proxyPosition[0]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.physicsProxy + 0x118, snapshot.proxyPosition[1]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.physicsProxy + 0x120, snapshot.proxyPosition[2]);
}

inline bool Capture(uintptr_t accessor, Snapshot& snapshot)
{
    snapshot = {};
    snapshot.accessor = accessor;
    if (!accessor ||
        !VehicleSeatTrace::ReadValue(accessor + 0x8, snapshot.mover) ||
        !snapshot.mover ||
        !VehicleSeatTrace::ReadValue(
            snapshot.mover + 0x48, snapshot.entity) ||
        !snapshot.entity) {
        return false;
    }

    snapshot.positionValid =
        VehicleSeatTrace::ReadValue(
            snapshot.entity + 0xE8, snapshot.position[0]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.entity + 0xF0, snapshot.position[1]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.entity + 0xF8, snapshot.position[2]);
    snapshot.entityOffsetValid =
        VehicleSeatTrace::ReadValue(
            snapshot.mover + 0x150, snapshot.entityOffset[0]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.mover + 0x154, snapshot.entityOffset[1]) &&
        VehicleSeatTrace::ReadValue(
            snapshot.mover + 0x158, snapshot.entityOffset[2]);
    CapturePhysicsPosition(snapshot);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x500, snapshot.frameMove[0]);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x504, snapshot.frameMove[1]);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x508, snapshot.frameMove[2]);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x2E0, snapshot.animationState);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x6F0, snapshot.physicsModePending);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x6F4, snapshot.requestedPhysicsMode);
    VehicleSeatTrace::ReadValue(
        snapshot.mover + 0x6F8, snapshot.appliedPhysicsMode);

    if (!VehicleSeatTrace::ReadValue(
            snapshot.entity + 0xC8, snapshot.model) ||
        !snapshot.model ||
        !VehicleSeatTrace::ReadValue(
            snapshot.model + 0x230, snapshot.rootMotionBuffer) ||
        snapshot.rootMotionBuffer > 1) {
        return snapshot.positionValid;
    }
    const uintptr_t motion = snapshot.model + 0xD0 +
        static_cast<uintptr_t>(snapshot.rootMotionBuffer) * 0xB0;
    snapshot.rootMotionValid =
        VehicleSeatTrace::ReadValue(motion, snapshot.rootMotion[0]) &&
        VehicleSeatTrace::ReadValue(motion + 0x4, snapshot.rootMotion[1]) &&
        VehicleSeatTrace::ReadValue(motion + 0x8, snapshot.rootMotion[2]);
    return snapshot.positionValid;
}

inline std::string Format(const char* label, const Snapshot& snapshot)
{
    std::ostringstream oss;
    oss << std::setprecision(10) << label
        << " accessor=" << VehicleSeatTrace::Hex(snapshot.accessor)
        << " mover=" << VehicleSeatTrace::Hex(snapshot.mover)
        << " entity=" << VehicleSeatTrace::Hex(snapshot.entity)
        << " pos=" << snapshot.position[0] << ','
        << snapshot.position[1] << ',' << snapshot.position[2]
        << " proxy=" << VehicleSeatTrace::Hex(snapshot.physicsProxy);
    if (snapshot.proxyPositionValid) {
        oss << " proxyPos=" << snapshot.proxyPosition[0] << ','
            << snapshot.proxyPosition[1] << ','
            << snapshot.proxyPosition[2];
        if (snapshot.positionValid) {
            oss << " entityProxyGap="
                << snapshot.position[0] - snapshot.proxyPosition[0] << ','
                << snapshot.position[1] - snapshot.proxyPosition[1] << ','
                << snapshot.position[2] - snapshot.proxyPosition[2];
        }
    } else {
        oss << " proxyPos=unavailable";
    }
    if (snapshot.entityOffsetValid) {
        oss << " entityOffset=" << snapshot.entityOffset[0] << ','
            << snapshot.entityOffset[1] << ','
            << snapshot.entityOffset[2];
    } else {
        oss << " entityOffset=unavailable";
    }
    oss
        << " anim=" << snapshot.animationState
        << " physics=" << static_cast<uint32_t>(
            snapshot.physicsModePending) << ':'
        << snapshot.requestedPhysicsMode << ':'
        << snapshot.appliedPhysicsMode
        << " frameMove=" << snapshot.frameMove[0] << ','
        << snapshot.frameMove[1] << ',' << snapshot.frameMove[2];
    if (snapshot.rootMotionValid) {
        oss << " root[" << snapshot.rootMotionBuffer << "]="
            << snapshot.rootMotion[0] << ',' << snapshot.rootMotion[1]
            << ',' << snapshot.rootMotion[2];
    } else {
        oss << " root=unavailable";
    }
    return oss.str();
}

} // namespace RideOffMoverSnapshot
