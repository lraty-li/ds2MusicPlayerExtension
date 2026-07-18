#include "pch.h"
#include "RideOnVtableDiscovery.h"

#include "FastBoardingSession.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOnVtableDiscovery {
namespace {

constexpr const char* kProcessAttachSignature =
    "4C 8B DC 55 56 49 8D AB ? ? ? ? 48 81 EC ? ? ? ? "
    "48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 81";
constexpr char kExpectedTypeName[] = ".?AVDSPlayerVehicleRideOnState@@";
constexpr uint32_t kStateEnterSlotIndex = 11;

using ProcessAttachFn = void(__fastcall*)(uintptr_t rideOn);
using RideOnEnterFn = int64_t(__fastcall*)(
    uintptr_t rideOn, uintptr_t a2, uintptr_t a3);

std::atomic<uintptr_t> g_lastRideOn{0};
std::atomic<uint32_t> g_lastBucket{UINT32_MAX};
const Logger* g_logger = nullptr;
ProcessAttachFn g_original = nullptr;
RideOnEnterFn g_originalEnter = nullptr;

int64_t __fastcall HookRideOnEnter(
    uintptr_t rideOn, uintptr_t a2, uintptr_t a3)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::Snapshot before = {};
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    const int64_t result = g_originalEnter(rideOn, a2, a3);
    FastBoardingSession::Begin(rideOn);
    VehicleSeatTrace::Snapshot after = {};
    const bool haveAfter = VehicleSeatTrace::CaptureSnapshot(plugin, after);

    std::ostringstream oss;
    oss << "RideOnEnterVtable original result=" << result
        << " haveBefore=" << haveBefore
        << " haveAfter=" << haveAfter;
    if (haveBefore)
        oss << " before{" << VehicleSeatTrace::FormatSnapshot(plugin, before) << " }";
    if (haveAfter)
        oss << " after{" << VehicleSeatTrace::FormatSnapshot(plugin, after) << " }";
    g_logger->Log(oss.str());
    return result;
}

bool StateChanged(
    const VehicleSeatTrace::Snapshot& before,
    const VehicleSeatTrace::Snapshot& after)
{
    return before.current != after.current || before.next != after.next ||
        before.stage != after.stage || before.b189 != after.b189 ||
        before.b18A != after.b18A || before.b18B != after.b18B ||
        before.b190 != after.b190 || before.b191 != after.b191 ||
        before.b192 != after.b192 || before.b381 != after.b381 ||
        before.b3B1 != after.b3B1;
}

void __fastcall HookProcessAttach(uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    uintptr_t owner = 0;
    uint32_t flagsBefore = 0;
    VehicleSeatTrace::Snapshot before = {};
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);
    VehicleSeatTrace::ReadValue(rideOn + 0xA0, owner);
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    if (owner)
        VehicleSeatTrace::ReadValue(owner + 0x7378, flagsBefore);

    g_original(rideOn);
    FastBoardingSession::ObserveProcessAttach(rideOn);

    uint32_t flagsAfter = 0;
    VehicleSeatTrace::Snapshot after = {};
    const bool haveAfter = VehicleSeatTrace::CaptureSnapshot(plugin, after);
    if (owner)
        VehicleSeatTrace::ReadValue(owner + 0x7378, flagsAfter);
    if (!haveBefore && !haveAfter)
        return;

    const VehicleSeatTrace::Snapshot& sample = haveAfter ? after : before;
    if (g_lastRideOn.exchange(rideOn) != rideOn)
        g_lastBucket.store(UINT32_MAX);
    const uint32_t bucket = static_cast<uint32_t>(sample.elapsed * 4.0f);
    const bool changed = haveBefore && haveAfter && StateChanged(before, after);
    if (g_lastBucket.exchange(bucket) == bucket && !changed)
        return;

    std::ostringstream oss;
    oss << "RideOnVtable original"
        << " ownerFlags=0x" << std::hex << flagsBefore
        << "->0x" << flagsAfter << std::dec
        << " haveBefore=" << haveBefore
        << " haveAfter=" << haveAfter;
    if (haveBefore)
        oss << " before{" << VehicleSeatTrace::FormatSnapshot(plugin, before) << " }";
    if (haveAfter)
        oss << " after{" << VehicleSeatTrace::FormatSnapshot(plugin, after) << " }";
    g_logger->Log(oss.str());
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    VtableLocator::Match match = {};
    if (!VtableLocator::FindUnique(
            gameModule, kProcessAttachSignature, match)) {
        logger.Log("RideOn vtable lookup failed pointerMatches=" +
            std::to_string(match.pointerMatches) + " rttiMatches=" +
            std::to_string(match.rttiMatches));
        return false;
    }
    std::ostringstream candidate;
    candidate << "RideOn ProcessAttach vtable candidate"
        << " target=" << VehicleSeatTrace::Hex(match.target)
        << " vtable=" << VehicleSeatTrace::Hex(match.vtable)
        << " slot=" << match.slotIndex
        << " subobjectOffset=0x" << std::hex << match.subobjectOffset
        << std::dec << " col=" << VehicleSeatTrace::Hex(match.col)
        << " type=" << match.typeName;
    logger.Log(candidate.str());
    if (match.typeName != kExpectedTypeName) {
        logger.Log("RideOn vtable type validation failed");
        return false;
    }

    g_logger = &logger;
    g_original = reinterpret_cast<ProcessAttachFn>(match.target);
    const uintptr_t enterSlot = match.vtable +
        kStateEnterSlotIndex * sizeof(uintptr_t);
    uintptr_t enterTarget = 0;
    if (!VehicleSeatTrace::ReadValue(enterSlot, enterTarget) || !enterTarget) {
        logger.Log("RideOn Enter vtable target read failed");
        return false;
    }
    g_originalEnter = reinterpret_cast<RideOnEnterFn>(enterTarget);
    if (!VtableLocator::SwapSlot(
            enterSlot, enterTarget,
            reinterpret_cast<void*>(&HookRideOnEnter))) {
        logger.Log("RideOn Enter vtable observer install failed");
        return false;
    }
    if (!VtableLocator::SwapSlot(
            match.slot, match.target,
            reinterpret_cast<void*>(&HookProcessAttach))) {
        VtableLocator::SwapSlot(
            enterSlot, reinterpret_cast<uintptr_t>(&HookRideOnEnter),
            reinterpret_cast<void*>(enterTarget));
        logger.Log("RideOn ProcessAttach vtable observer install failed");
        return false;
    }
    logger.Log("RideOn Enter vtable observer installed slot=" +
        std::to_string(kStateEnterSlotIndex) + " target=" +
        VehicleSeatTrace::Hex(enterTarget));
    logger.Log("RideOn ProcessAttach vtable observer installed");
    return true;
}

} // namespace RideOnVtableDiscovery
