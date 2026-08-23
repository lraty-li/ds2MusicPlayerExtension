#include "pch.h"
#include "RideOffNativeDetach.h"

#include "PatternScan.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>

namespace RideOffNativeDetach {
namespace {

constexpr const char* kDetachSignature =
    "40 53 48 81 EC A0 00 00 00 48 8B D9 84 D2";
constexpr uintptr_t kDetachGateOffset = 0x371;

using DetachFn = void(__fastcall*)(uintptr_t runtime, uint8_t forceDetach);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
DetachFn g_detach = nullptr;

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
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kDetachSignature);
    if (!target)
        return false;

    g_logger = &logger;
    g_detach = reinterpret_cast<DetachFn>(target);
    RideOffSession::ReportComponentReady(
        RideOffSession::kNativeDetachComponent);
    g_started.store(true, std::memory_order_release);
    logger.Log("FastRideOff native detach resolved");
    return true;
}

bool Request(uintptr_t runtime)
{
    if (!runtime || !g_detach)
        return false;

    uint8_t savedGate = 0;
    if (!VehicleSeatTrace::ReadValue(
            runtime + kDetachGateOffset, savedGate) ||
        !VehicleSeatTrace::WriteValue<uint8_t>(
            runtime + kDetachGateOffset, 1)) {
        return false;
    }

    g_detach(runtime, 0);
    if (!VehicleSeatTrace::WriteValue<uint8_t>(
            runtime + kDetachGateOffset, savedGate)) {
        g_logger->Log("FastRideOff native detach gate restore failed");
    }
    return true;
}

} // namespace RideOffNativeDetach
