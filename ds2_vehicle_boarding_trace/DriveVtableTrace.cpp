#include "pch.h"
#include "DriveVtableTrace.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <cstdint>
#include <sstream>

namespace DriveVtableTrace {
namespace {

constexpr const char* kDriveEnterSignature =
    "40 57 41 56 48 83 EC ? 48 8B 81 ? ? ? ? 48 8B F9 48 8B 88";
constexpr char kExpectedTypeName[] = ".?AVDSPlayerVehicleDriveState@@";

using DriveEnterFn = int64_t(__fastcall*)(
    uintptr_t driveState, uintptr_t a2, uintptr_t a3);

const Logger* g_logger = nullptr;
DriveEnterFn g_original = nullptr;

int64_t __fastcall HookDriveEnter(
    uintptr_t driveState, uintptr_t a2, uintptr_t a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::Snapshot before = {};
    VehicleSeatTrace::ReadValue(driveState + 0x88, plugin);
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    const int64_t result = g_original(driveState, a2, a3);
    FastBoardingSession::ObserveDriveEnter(driveState);
    VehicleSeatTrace::Snapshot after = {};
    const bool haveAfter = VehicleSeatTrace::CaptureSnapshot(plugin, after);

    std::ostringstream oss;
    oss << "DriveVtable original result=" << result
        << " haveBefore=" << haveBefore
        << " haveAfter=" << haveAfter;
    if (haveBefore)
        oss << " before{" << VehicleSeatTrace::FormatSnapshot(plugin, before) << " }";
    if (haveAfter)
        oss << " after{" << VehicleSeatTrace::FormatSnapshot(plugin, after) << " }";
    g_logger->Log(oss.str());
    return result;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    VtableLocator::Match match = {};
    if (!VtableLocator::FindUnique(gameModule, kDriveEnterSignature, match)) {
        logger.Log("Drive vtable lookup failed pointerMatches=" +
            std::to_string(match.pointerMatches) + " rttiMatches=" +
            std::to_string(match.rttiMatches));
        return false;
    }
    std::ostringstream candidate;
    candidate << "Drive Enter vtable candidate"
        << " target=" << VehicleSeatTrace::Hex(match.target)
        << " vtable=" << VehicleSeatTrace::Hex(match.vtable)
        << " slot=" << match.slotIndex
        << " subobjectOffset=0x" << std::hex << match.subobjectOffset
        << std::dec << " col=" << VehicleSeatTrace::Hex(match.col)
        << " type=" << match.typeName;
    logger.Log(candidate.str());
    if (match.typeName != kExpectedTypeName) {
        logger.Log("Drive vtable type validation failed");
        return false;
    }

    g_logger = &logger;
    g_original = reinterpret_cast<DriveEnterFn>(match.target);
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookDriveEnter))) {
        logger.Log("Drive Enter vtable observer install failed");
        return false;
    }
    logger.Log("Drive Enter vtable observer installed");
    return true;
}

} // namespace DriveVtableTrace
