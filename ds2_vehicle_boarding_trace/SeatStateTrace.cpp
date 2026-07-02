#include "pch.h"
#include "SeatStateTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace SeatStateTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kSeatStateRva = 0x140F9B670ull - kImageBase;
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
    SeatFields after = {};
    if (seatObject)
        CaptureSeatFields(seatObject, before);

    if (seatObject) {
        CaptureSeatFields(seatObject, after);
        std::ostringstream oss;
        oss << "SeatStateUpdate suppressed approach=" << static_cast<int>(approach)
            << " b3B1=" << static_cast<int>(b3B1)
            << " seatObject=" << VehicleSeatTrace::Hex(seatObject)
            << " p12F8=" << VehicleSeatTrace::Hex(before.ptr12F8)
            << "->" << VehicleSeatTrace::Hex(after.ptr12F8)
            << " v125C=" << before.value125C << "->" << after.value125C
            << " v1310=" << before.value1310 << "->" << after.value1310
            << " v1314=" << before.value1314 << "->" << after.value1314
            << " result=1";
        g_logger->Log(oss.str());
        return 1;
    }

    return g_originalSeatState(rideOn, approach, b3B1);
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

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kSeatStateRva;
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
