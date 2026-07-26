#include "pch.h"
#include "TruckRideOffSuppressor.h"

#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace TruckRideOffSuppressor {
namespace {

std::atomic<uint32_t> g_loggedSession{0};

bool CompareExchangeRequest(uintptr_t address)
{
    __try {
        return InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(address), -1, 7) == 7;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool TrySuppress(uintptr_t truck, const Logger& logger)
{
    const uint32_t session = RideOffSession::ActiveId();
    uintptr_t controller = 0;
    uintptr_t playback = 0;
    int32_t current = 0;
    int32_t requested = 0;
    uint8_t playbackState = 0;
    if (!session || !truck ||
        !VehicleSeatTrace::ReadValue(truck + 0x12F8, controller) ||
        !VehicleSeatTrace::ReadValue(truck + 0x1310, current) ||
        !VehicleSeatTrace::ReadValue(truck + 0x1314, requested) ||
        current != 0 || requested != 7 || !controller ||
        !VehicleSeatTrace::ReadValue(controller + 0x50, playback) ||
        !playback ||
        !VehicleSeatTrace::ReadValue(
            playback + 0x18, playbackState) ||
        playbackState != 2 ||
        !CompareExchangeRequest(truck + 0x1314)) {
        return false;
    }

    if (g_loggedSession.exchange(
            session, std::memory_order_acq_rel) != session) {
        std::ostringstream oss;
        oss << "FastRideOff TruckSeat dismount request suppressed"
            << " session=" << session
            << " truck=" << VehicleSeatTrace::Hex(truck)
            << " current=" << current
            << " request=" << requested
            << " playbackState="
            << static_cast<uint32_t>(playbackState);
        logger.Log(oss.str());
    }
    return true;
}

} // namespace TruckRideOffSuppressor
