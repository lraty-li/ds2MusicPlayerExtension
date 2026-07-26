#include "pch.h"
#include "RideOffSession.h"

#include <atomic>
#include <cstdint>

namespace RideOffSession {
namespace {

constexpr uint64_t kWindowMs = 5000;

std::atomic<uint32_t> g_id{0};
std::atomic<uintptr_t> g_rideOff{0};
std::atomic<uint64_t> g_deadline{0};
std::atomic<uint32_t> g_cutInActionHash{0};
std::atomic<bool> g_cutInFastForwarded{false};
std::atomic<bool> g_finalizerForced{false};
std::atomic<bool> g_nativeExitRequested{false};
thread_local uintptr_t t_rideOffUpdate = 0;

bool InWindow()
{
    return g_rideOff.load(std::memory_order_acquire) != 0 &&
        GetTickCount64() <= g_deadline.load(std::memory_order_relaxed);
}

} // namespace

void Begin(uintptr_t rideOff)
{
    g_rideOff.store(0, std::memory_order_release);
    g_cutInActionHash.store(0, std::memory_order_relaxed);
    g_cutInFastForwarded.store(false, std::memory_order_relaxed);
    g_finalizerForced.store(false, std::memory_order_relaxed);
    g_nativeExitRequested.store(false, std::memory_order_relaxed);
    if (!rideOff)
        return;
    const uint64_t now = GetTickCount64();
    g_deadline.store(now + kWindowMs, std::memory_order_relaxed);
    g_id.fetch_add(1, std::memory_order_acq_rel);
    g_rideOff.store(rideOff, std::memory_order_release);
}

void ObserveCutInAction(uintptr_t rideOff, uint32_t actionHash)
{
    if (actionHash && InWindow() &&
        rideOff == g_rideOff.load(std::memory_order_acquire)) {
        g_cutInActionHash.store(actionHash, std::memory_order_release);
    }
}

bool ShouldFastForwardCutIn(uint32_t actionHash)
{
    return actionHash && InWindow() &&
        actionHash == g_cutInActionHash.load(std::memory_order_acquire) &&
        !g_cutInFastForwarded.load(std::memory_order_acquire);
}

bool MarkCutInFastForwarded(uint32_t actionHash)
{
    return ShouldFastForwardCutIn(actionHash) &&
        !g_cutInFastForwarded.exchange(true, std::memory_order_acq_rel);
}

bool IsActiveCutInAction(uint32_t actionHash)
{
    return actionHash && InWindow() &&
        actionHash == g_cutInActionHash.load(std::memory_order_acquire);
}

bool MarkFinalizerForced()
{
    return CurrentId() &&
        !g_finalizerForced.exchange(true, std::memory_order_acq_rel);
}

bool MarkNativeExitRequested()
{
    return InWindow() &&
        !g_nativeExitRequested.exchange(true, std::memory_order_acq_rel);
}

uint32_t ActiveId()
{
    return InWindow() ? g_id.load(std::memory_order_acquire) : 0;
}

uintptr_t EnterUpdate(uintptr_t rideOff)
{
    const uintptr_t previous = t_rideOffUpdate;
    t_rideOffUpdate = rideOff;
    return previous;
}

void LeaveUpdate(uintptr_t previousRideOff)
{
    t_rideOffUpdate = previousRideOff;
}

uint32_t CurrentId()
{
    if (!t_rideOffUpdate ||
        t_rideOffUpdate != g_rideOff.load(std::memory_order_acquire) ||
        GetTickCount64() > g_deadline.load(std::memory_order_relaxed)) {
        return 0;
    }
    return g_id.load(std::memory_order_acquire);
}

} // namespace RideOffSession
