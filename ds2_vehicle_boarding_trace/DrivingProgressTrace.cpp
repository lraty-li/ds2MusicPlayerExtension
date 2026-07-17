#include "pch.h"
#include "DrivingProgressTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace DrivingProgressTrace {
namespace {

constexpr const char* kUpdateTransitionSignature =
    "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 "
    "48 81 EC ? ? ? ? C5 F8 29 70 ? C5 F8 29 78 ? C5 78 29 40 ? "
    "C5 78 29 48 ? C5 78 29 50 ? 45 33 E4";
constexpr size_t kPatchLen = 15;

using UpdateTransitionFn = uint64_t(__fastcall*)(uintptr_t callback, float deltaSeconds);

std::atomic<bool> g_started{false};
std::atomic<uintptr_t> g_tracedCallback{0};
std::atomic<uint32_t> g_sampleCount{0};
std::atomic<int> g_lastProgressBucket{-1};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
UpdateTransitionFn g_original = nullptr;

uintptr_t ToGameRva(uintptr_t address)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_module);
    return address >= base ? address - base : address;
}

bool IsActiveBoardingCallback(uintptr_t callback, uintptr_t& rideOn)
{
    rideOn = RideOnEnterInterceptor::ActiveBoardingRideOn();
    if (!rideOn)
        return false;

    uintptr_t plugin = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin) || !plugin)
        return false;
    return callback == plugin + 0x2A8;
}

void LogSample(
    uintptr_t callback,
    uintptr_t rideOn,
    uintptr_t caller,
    float deltaSeconds)
{
    float progress8 = 0.0f;
    float progressC = 0.0f;
    float progress14 = 0.0f;
    uint8_t flag24 = 0;
    uint8_t flag25 = 0;
    uint32_t flags8C = 0;
    VehicleSeatTrace::ReadValue(callback + 0x08, progress8);
    VehicleSeatTrace::ReadValue(callback + 0x0C, progressC);
    VehicleSeatTrace::ReadValue(callback + 0x14, progress14);
    VehicleSeatTrace::ReadValue(callback + 0x24, flag24);
    VehicleSeatTrace::ReadValue(callback + 0x25, flag25);
    VehicleSeatTrace::ReadValue(callback + 0x8C, flags8C);

    const uint32_t sample = g_sampleCount.fetch_add(1);
    const int bucket = static_cast<int>(progress14 * 10.0f);
    const int previousBucket = g_lastProgressBucket.exchange(bucket);
    if (sample >= 6 && sample % 12 != 0 && bucket == previousBucket)
        return;

    float elapsed = 0.0f;
    VehicleSeatTrace::ReadValue(rideOn + 0x180, elapsed);
    std::ostringstream oss;
    oss << "DrivingProgress sample=" << sample
        << " callerRva=" << VehicleSeatTrace::Hex(ToGameRva(caller))
        << " dt=" << deltaSeconds
        << " elapsed=" << elapsed
        << " p8=" << progress8
        << " pC=" << progressC
        << " p14=" << progress14
        << " f24=" << static_cast<int>(flag24)
        << " f25=" << static_cast<int>(flag25)
        << " flags8C=0x" << std::hex << flags8C;
    g_logger->Log(oss.str());
}

uint64_t __fastcall HookUpdateTransition(uintptr_t callback, float deltaSeconds)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint64_t result = g_original(callback, deltaSeconds);

    uintptr_t rideOn = 0;
    if (!IsActiveBoardingCallback(callback, rideOn))
        return result;

    if (g_tracedCallback.exchange(callback) != callback) {
        g_sampleCount.store(0);
        g_lastProgressBucket.store(-1);
        g_logger->Log("DrivingProgress active callback=" +
            VehicleSeatTrace::Hex(callback));
    }
    LogSample(callback, rideOn, caller, deltaSeconds);
    return result;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;
    g_module = gameModule;
    g_logger = &logger;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
        return false;
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kUpdateTransitionSignature);
    if (!target) {
        logger.Log("DrivingProgress signature not found or not unique");
        return false;
    }

    void* trampoline = JumpHook::MakeTrampoline(target, kPatchLen);
    if (!trampoline)
        return false;
    g_original = reinterpret_cast<UpdateTransitionFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookUpdateTransition), kPatchLen)) {
        return false;
    }

    logger.Log("DrivingProgress read-only hook installed at " +
        VehicleSeatTrace::Hex(target));
    return true;
}

} // namespace DrivingProgressTrace
