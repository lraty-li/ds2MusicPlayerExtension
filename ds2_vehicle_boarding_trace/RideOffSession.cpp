#include "pch.h"
#include "RideOffSession.h"

#include "RideOffNativeDetach.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>

namespace RideOffSession {
namespace {

constexpr uint64_t kWindowMs = 5000;
constexpr uint32_t kRequiredComponents =
    kPoseComponent | kGraphEndpointComponent | kNativeDetachComponent |
    kQueueClockComponent;

std::atomic<uint32_t> g_components{0};
std::atomic<uint32_t> g_id{0};
std::atomic<uintptr_t> g_rideOff{0};
std::atomic<uintptr_t> g_player{0};
std::atomic<uintptr_t> g_plugin{0};
std::atomic<uintptr_t> g_moverAccessor{0};
std::atomic<uintptr_t> g_graphManager{0};
std::atomic<uint64_t> g_startedAt{0};
std::atomic<uint64_t> g_deadline{0};
std::atomic<uint32_t> g_cutInActionHash{0};
std::atomic<uintptr_t> g_endpointOutput{0};
std::atomic<uintptr_t> g_endpointDescriptor{0};
std::atomic<bool> g_endpointClaimed{false};
std::atomic<bool> g_endpointComplete{false};
std::atomic<bool> g_poseApplied{false};
std::atomic<bool> g_exitRequestClaimed{false};
std::atomic<bool> g_cutInFastForwarded{false};
std::atomic<bool> g_finalizerGateOpened{false};
thread_local uintptr_t t_rideOffUpdate = 0;

bool ComponentsReady()
{
    return (g_components.load(std::memory_order_acquire) &
        kRequiredComponents) == kRequiredComponents;
}

bool InWindow()
{
    return g_rideOff.load(std::memory_order_acquire) != 0 &&
        GetTickCount64() <= g_deadline.load(std::memory_order_relaxed);
}

bool MatchesEndpoint(
    uintptr_t output, uintptr_t descriptor, uint32_t session)
{
    return session && session == ActiveId() &&
        g_endpointClaimed.load(std::memory_order_acquire) &&
        output == g_endpointOutput.load(std::memory_order_acquire) &&
        descriptor ==
            g_endpointDescriptor.load(std::memory_order_acquire);
}

} // namespace

void ReportComponentReady(Component component)
{
    g_components.fetch_or(component, std::memory_order_acq_rel);
}

uint32_t Begin(uintptr_t rideOff, uintptr_t player)
{
    g_rideOff.store(0, std::memory_order_release);
    g_player.store(0, std::memory_order_relaxed);
    g_plugin.store(0, std::memory_order_relaxed);
    g_moverAccessor.store(0, std::memory_order_relaxed);
    g_graphManager.store(0, std::memory_order_relaxed);
    g_startedAt.store(0, std::memory_order_relaxed);
    g_cutInActionHash.store(0, std::memory_order_relaxed);
    g_endpointOutput.store(0, std::memory_order_relaxed);
    g_endpointDescriptor.store(0, std::memory_order_relaxed);
    g_endpointClaimed.store(false, std::memory_order_relaxed);
    g_endpointComplete.store(false, std::memory_order_relaxed);
    g_poseApplied.store(false, std::memory_order_relaxed);
    g_exitRequestClaimed.store(false, std::memory_order_relaxed);
    g_cutInFastForwarded.store(false, std::memory_order_relaxed);
    g_finalizerGateOpened.store(false, std::memory_order_relaxed);
    if (!rideOff || !player || !ComponentsReady())
        return 0;

    uintptr_t plugin = 0;
    uintptr_t moverAccessor = 0;
    uintptr_t graphManager = 0;
    if (!VehicleSeatTrace::ReadValue(rideOff + 0x88, plugin) || !plugin ||
        !VehicleSeatTrace::ReadValue(
            rideOff + 0xB0, moverAccessor) || !moverAccessor ||
        !VehicleSeatTrace::ReadValue(
            player + 0x8A8, graphManager) || !graphManager) {
        return 0;
    }
    const uint64_t now = GetTickCount64();
    g_startedAt.store(now, std::memory_order_relaxed);
    g_deadline.store(now + kWindowMs, std::memory_order_relaxed);
    g_plugin.store(plugin, std::memory_order_relaxed);
    g_moverAccessor.store(moverAccessor, std::memory_order_relaxed);
    g_graphManager.store(graphManager, std::memory_order_relaxed);
    g_player.store(player, std::memory_order_release);
    const uint32_t session =
        g_id.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_rideOff.store(rideOff, std::memory_order_release);
    return session;
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
    return actionHash && CompletionReady() &&
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

bool TryClaimGraphEndpoint(
    uintptr_t output, uintptr_t descriptor, uint32_t& session)
{
    session = ActiveId();
    if (!session || !output || !descriptor ||
        g_endpointClaimed.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }
    g_endpointOutput.store(output, std::memory_order_release);
    g_endpointDescriptor.store(descriptor, std::memory_order_release);
    return session == ActiveId();
}

bool IsClaimedGraphEndpoint(
    uintptr_t output, uintptr_t descriptor, uint32_t& session)
{
    session = ActiveId();
    return MatchesEndpoint(output, descriptor, session);
}

void ReleaseGraphEndpointClaim(
    uintptr_t output, uintptr_t descriptor, uint32_t session)
{
    if (!MatchesEndpoint(output, descriptor, session) ||
        g_endpointComplete.load(std::memory_order_acquire)) {
        return;
    }
    g_endpointOutput.store(0, std::memory_order_relaxed);
    g_endpointDescriptor.store(0, std::memory_order_relaxed);
    g_endpointClaimed.store(false, std::memory_order_release);
}

bool MarkGraphEndpointComplete(
    uintptr_t output, uintptr_t descriptor, uint32_t session)
{
    return MatchesEndpoint(output, descriptor, session) &&
        !g_endpointComplete.exchange(true, std::memory_order_acq_rel);
}

bool TryRequestNativeExitBeforePoseConsumption(
    uintptr_t moverAccessor, uintptr_t& plugin)
{
    plugin = 0;
    if (!moverAccessor || !GraphEndpointComplete() ||
        moverAccessor !=
            g_moverAccessor.load(std::memory_order_acquire)) {
        return false;
    }
    bool expected = false;
    if (!g_exitRequestClaimed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    plugin = g_plugin.load(std::memory_order_acquire);
    uint8_t current = UINT8_MAX;
    uint16_t pending = UINT16_MAX;
    const bool ready = plugin &&
        VehicleSeatTrace::ReadValue(plugin + 0x118, current) &&
        VehicleSeatTrace::ReadValue(plugin + 0x11A, pending) &&
        current == 3 && pending == 3;
    if (!ready ||
        !VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, 0) ||
        !RideOffNativeDetach::Request(plugin)) {
        if (ready)
            VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, pending);
        plugin = 0;
        g_exitRequestClaimed.store(false, std::memory_order_release);
        return false;
    }
    VehicleSeatTrace::WriteValue<uint16_t>(plugin + 0x11A, 0);
    return true;
}

bool MarkPostGraphPoseConsumed(uintptr_t moverAccessor)
{
    return moverAccessor &&
        moverAccessor == g_moverAccessor.load(std::memory_order_acquire) &&
        GraphEndpointComplete() &&
        !g_poseApplied.exchange(true, std::memory_order_acq_rel);
}

bool GraphEndpointClaimed()
{
    return InWindow() &&
        g_endpointClaimed.load(std::memory_order_acquire);
}

bool GraphEndpointComplete()
{
    return InWindow() &&
        g_endpointComplete.load(std::memory_order_acquire);
}

bool CompletionReady()
{
    return GraphEndpointComplete() &&
        g_poseApplied.load(std::memory_order_acquire);
}

bool MarkFinalizerGateOpened()
{
    return CompletionReady() &&
        !g_finalizerGateOpened.exchange(true, std::memory_order_acq_rel);
}

uint32_t ActiveId()
{
    return InWindow() ? g_id.load(std::memory_order_acquire) : 0;
}

uintptr_t ActivePlayer()
{
    return InWindow() ?
        g_player.load(std::memory_order_acquire) : 0;
}

bool MatchesGraphManager(uintptr_t manager)
{
    return manager && InWindow() &&
        manager == g_graphManager.load(std::memory_order_acquire);
}

uint64_t ElapsedMs()
{
    const uint64_t startedAt = g_startedAt.load(std::memory_order_relaxed);
    const uint64_t now = GetTickCount64();
    return startedAt && now >= startedAt ? now - startedAt : 0;
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
        !InWindow()) {
        return 0;
    }
    return g_id.load(std::memory_order_acquire);
}

} // namespace RideOffSession
