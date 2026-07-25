#include "pch.h"
#include "RideOffDescriptorTrace.h"

#include "VehicleSnapshot.h"

#include <atomic>
#include <sstream>

namespace RideOffDescriptorTrace {
namespace {

std::atomic<uint32_t> g_loggedSession{0};
std::atomic<int> g_logBudget{0};

} // namespace

void Observe(
    const Logger& logger, uint32_t session, uintptr_t fullGame,
    uintptr_t caller, uintptr_t descriptor, uint8_t mode, float timeScale,
    uint8_t evaluatePose, float duration, float syncDuration, uint8_t reachedEnd)
{
    uint32_t observed = g_loggedSession.load(std::memory_order_acquire);
    if (observed != session && g_loggedSession.compare_exchange_strong(
            observed, session, std::memory_order_acq_rel)) {
        g_logBudget.store(32, std::memory_order_release);
    }
    if (g_logBudget.fetch_sub(1, std::memory_order_acq_rel) <= 0)
        return;

    std::ostringstream oss;
    oss << "RideOff descriptor observed"
        << " session=" << session
        << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
        << " callerRva=" << VehicleSeatTrace::Hex(caller - fullGame)
        << " mode=" << static_cast<uint32_t>(mode)
        << " scale=" << timeScale
        << " pose=" << static_cast<uint32_t>(evaluatePose)
        << " duration=" << duration
        << " sync=" << syncDuration
        << " end=" << static_cast<uint32_t>(reachedEnd);
    logger.Log(oss.str());
}

} // namespace RideOffDescriptorTrace
