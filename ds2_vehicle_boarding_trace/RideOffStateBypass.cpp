#include "pch.h"
#include "RideOffStateBypass.h"

#include "PatternScan.h"
#include "VehicleSnapshot.h"
#include "VtableLocator.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOffStateBypass {
namespace {

constexpr char kExpectedTypeName[] =
    ".?AVDSPlayerRideVehicleActionPlugin@@";
constexpr uint32_t kCommitPendingStateSlotIndex = 22;
constexpr uintptr_t kCurrentStateOffset = 0x118;
constexpr uintptr_t kPendingStateOffset = 0x11A;
constexpr uintptr_t kPendingFlagOffset = 0x11B;
constexpr uintptr_t kDetachGateOffset = 0x371;
constexpr uint8_t kFreeState = 0;
constexpr uint8_t kDriveState = 2;
constexpr uint8_t kRideOffState = 3;
constexpr const char* kDetachSignature =
    "40 53 48 81 EC A0 00 00 00 48 8B D9 84 D2";

using CommitPendingStateFn = int64_t(__fastcall*)(uintptr_t plugin);
using DetachFn = void(__fastcall*)(uintptr_t runtime, uint8_t forceDetach);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
CommitPendingStateFn g_originalCommit = nullptr;
DetachFn g_detach = nullptr;

bool ReadState(uintptr_t plugin, uint8_t& current, uint8_t& pending)
{
    return plugin &&
        VehicleSeatTrace::ReadValue(
            plugin + kCurrentStateOffset, current) &&
        VehicleSeatTrace::ReadValue(
            plugin + kPendingStateOffset, pending);
}

void LogBypassResult(uintptr_t plugin)
{
    uint8_t current = 0xFF;
    uint8_t pending = 0xFF;
    uint8_t flag = 0xFF;
    VehicleSeatTrace::ReadValue(plugin + kCurrentStateOffset, current);
    VehicleSeatTrace::ReadValue(plugin + kPendingStateOffset, pending);
    VehicleSeatTrace::ReadValue(plugin + kPendingFlagOffset, flag);
    std::ostringstream oss;
    oss << "FastRideOff pre-RideOff bypass complete"
        << " current=" << static_cast<uint32_t>(current)
        << " next=" << static_cast<uint32_t>(pending)
        << " flag=" << static_cast<uint32_t>(flag);
    g_logger->Log(oss.str());
}

int64_t __fastcall HookCommitPendingState(uintptr_t plugin)
{
    uint8_t current = 0;
    uint8_t pending = 0;
    uint8_t savedGate = 0;
    if (!ReadState(plugin, current, pending) ||
        current != kDriveState || pending != kRideOffState ||
        !VehicleSeatTrace::ReadValue(
            plugin + kDetachGateOffset, savedGate)) {
        return g_originalCommit(plugin);
    }
    if (!VehicleSeatTrace::WriteValue<uint8_t>(
            plugin + kDetachGateOffset, 1)) {
        return g_originalCommit(plugin);
    }
    if (!VehicleSeatTrace::WriteValue<uint8_t>(
            plugin + kPendingStateOffset, kFreeState)) {
        VehicleSeatTrace::WriteValue<uint8_t>(
            plugin + kDetachGateOffset, savedGate);
        return g_originalCommit(plugin);
    }

    g_logger->Log(
        "FastRideOff pre-RideOff operation-21 detach requested"
        " current=2 next=3->0");
    g_detach(plugin, 0);
    VehicleSeatTrace::WriteValue<uint8_t>(
        plugin + kDetachGateOffset, savedGate);
    VehicleSeatTrace::WriteValue<uint8_t>(
        plugin + kPendingStateOffset, kFreeState);

    const int64_t result = g_originalCommit(plugin);
    LogBypassResult(plugin);
    return result;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(
            gameModule, ".text", textStart, textSize)) {
        return false;
    }
    const uintptr_t detachTarget = PatternScan::FindUnique(
        textStart, textSize, kDetachSignature);
    if (!detachTarget)
        return false;

    VtableLocator::Match commit = {};
    if (!VtableLocator::FindUniqueByRtti(
            gameModule, kExpectedTypeName, 0,
            kCommitPendingStateSlotIndex, commit)) {
        return false;
    }

    g_logger = &logger;
    g_detach = reinterpret_cast<DetachFn>(detachTarget);
    g_originalCommit =
        reinterpret_cast<CommitPendingStateFn>(commit.target);
    if (!VtableLocator::SwapSlot(
            commit.slot, commit.target,
            reinterpret_cast<void*>(&HookCommitPendingState))) {
        return false;
    }

    g_started.store(true, std::memory_order_release);
    logger.Log("FastRideOff pre-RideOff state bypass installed");
    return true;
}

} // namespace RideOffStateBypass
