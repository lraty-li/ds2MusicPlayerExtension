#include "pch.h"
#include "SeatStateTrace.h"
#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace SeatStateTrace {
namespace {

constexpr const char* kSeatStateSignature =
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? "
    "48 8B 81 ? ? ? ? 48 8B F9 41 0F B6 F0";
constexpr size_t kSeatStatePatchLen = 15;

using SeatStateFn = uint8_t(__fastcall*)(uintptr_t rideOn, uint8_t approach, uint8_t b3B1);

struct SeatFields {
    uintptr_t ptr12F8 = 0;
    uint32_t value125C = 0;
    uint32_t value1310 = 0;
    uint32_t value1314 = 0;
};

std::atomic<bool> g_started{false};
std::atomic<uintptr_t> g_lastRideOn{0};
std::atomic<uintptr_t> g_lastSeatObject{0};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
SeatStateFn g_originalSeatState = nullptr;

void CaptureSeatFields(uintptr_t seatObject, SeatFields& fields)
{
    VehicleSeatTrace::ReadValue(seatObject + 0x12F8, fields.ptr12F8);
    VehicleSeatTrace::ReadValue(seatObject + 0x125C, fields.value125C);
    VehicleSeatTrace::ReadValue(seatObject + 0x1310, fields.value1310);
    VehicleSeatTrace::ReadValue(seatObject + 0x1314, fields.value1314);
}

uint8_t __fastcall HookSeatState(uintptr_t rideOn, uint8_t approach, uint8_t b3B1)
{
    const uintptr_t seatObject =
        g_lastRideOn.load() == rideOn ? g_lastSeatObject.load() : 0;
    SeatFields before = {};
    if (seatObject)
        CaptureSeatFields(seatObject, before);

    const uint8_t result = g_originalSeatState(rideOn, approach, b3B1);

    std::ostringstream oss;
    oss << "SeatStateUpdate original approach=" << static_cast<int>(approach)
        << " b3B1=" << static_cast<int>(b3B1)
        << " rideOn=" << VehicleSeatTrace::Hex(rideOn);
    if (seatObject) {
        oss << " seatObject=" << VehicleSeatTrace::Hex(seatObject)
            << " p12F8=" << VehicleSeatTrace::Hex(before.ptr12F8)
            << " v125C=" << before.value125C
            << " v1310=" << before.value1310
            << " v1314=" << before.value1314;
    }
    oss << " result=" << static_cast<int>(result);
    g_logger->Log(oss.str());
    return result;
}

} // namespace

void RememberSeatObject(uintptr_t rideOn, uintptr_t seatObject)
{
    g_lastSeatObject.store(seatObject);
    g_lastRideOn.store(rideOn);
}

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize)) {
        logger.Log("SeatState .text unavailable");
        return false;
    }
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kSeatStateSignature);
    if (!target) {
        logger.Log("SeatState signature not found");
        return false;
    }
    {
        std::ostringstream oss;
        oss << "SeatState resolved at " << VehicleSeatTrace::Hex(target);
        logger.Log(oss.str());
    }
    void* trampoline = JumpHook::MakeTrampoline(target, kSeatStatePatchLen);
    if (!trampoline) {
        logger.Log("InstallSeatStateHook trampoline failed");
        return false;
    }
    g_originalSeatState = reinterpret_cast<SeatStateFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookSeatState), kSeatStatePatchLen)) {
        logger.Log("InstallSeatStateHook failed");
        return false;
    }
    return true;
}

} // namespace SeatStateTrace
