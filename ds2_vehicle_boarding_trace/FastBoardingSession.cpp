#include "pch.h"
#include "FastBoardingSession.h"

#include "VehicleSnapshot.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace FastBoardingSession {
namespace {

constexpr uint32_t kBoardingCompleteEvent = 186;
constexpr uint64_t kSessionWindowMs = 5000;
constexpr uint32_t kRequiredComponents = kScopeComponent | kGraphComponent |
    kCutInComponent | kAnimationComponent | kPoseComponent;

std::atomic<uint32_t> g_components{0};
std::atomic<uint32_t> g_sessionId{0};
std::atomic<uintptr_t> g_rideOn{0};
std::atomic<uintptr_t> g_plugin{0};
std::atomic<uintptr_t> g_graphManager{0};
std::atomic<uint64_t> g_deadline{0};
std::atomic<bool> g_ready{false};
std::atomic<bool> g_driveEntered{false};
std::atomic<bool> g_postDrivePoseReady{false};
std::atomic<bool> g_graphForced{false};
std::atomic<bool> g_nativeCompletionSeen{false};
std::atomic<bool> g_nativeCompletionUpdateReturned{false};
std::atomic<bool> g_animationCompleted{false};
std::atomic<bool> g_cutInAttempted{false};
std::atomic<uintptr_t> g_cutInCamera{0};
std::atomic<uint32_t> g_cutInActionHash{0};
thread_local uintptr_t t_rideOnUpdate = 0;

bool InWindow()
{
    return g_rideOn.load(std::memory_order_acquire) != 0 &&
        GetTickCount64() <= g_deadline.load(std::memory_order_relaxed);
}

bool ReadLiveSnapshot(VehicleSeatTrace::Snapshot& snapshot)
{
    const uintptr_t rideOn = g_rideOn.load(std::memory_order_acquire);
    const uintptr_t plugin = g_plugin.load(std::memory_order_relaxed);
    return rideOn && plugin &&
        VehicleSeatTrace::CaptureSnapshot(plugin, snapshot) &&
        snapshot.rideOn == rideOn;
}

bool IsReadyRideOn()
{
    if (!AllComponentsReady() ||
        !g_ready.load(std::memory_order_acquire) || !InWindow())
        return false;
    VehicleSeatTrace::Snapshot snapshot = {};
    if (!ReadLiveSnapshot(snapshot) || snapshot.stage != 2)
        return false;
    if (snapshot.current == 1 &&
        (snapshot.next == 1 || snapshot.next == 2)) {
        return true;
    }
    return g_driveEntered.load(std::memory_order_relaxed) &&
        snapshot.current == 2;
}

} // namespace

void ReportComponentReady(uint32_t component)
{
    g_components.fetch_or(component, std::memory_order_acq_rel);
}

bool AllComponentsReady()
{
    return (g_components.load(std::memory_order_acquire) &
        kRequiredComponents) == kRequiredComponents;
}

void Begin(uintptr_t rideOn)
{
    g_rideOn.store(0, std::memory_order_release);
    g_ready.store(false, std::memory_order_relaxed);
    g_driveEntered.store(false, std::memory_order_relaxed);
    g_postDrivePoseReady.store(false, std::memory_order_relaxed);
    g_graphForced.store(false, std::memory_order_relaxed);
    g_nativeCompletionSeen.store(false, std::memory_order_relaxed);
    g_nativeCompletionUpdateReturned.store(false,
        std::memory_order_relaxed);
    g_animationCompleted.store(false, std::memory_order_relaxed);
    g_cutInAttempted.store(false, std::memory_order_relaxed);
    g_cutInCamera.store(0, std::memory_order_relaxed);
    g_cutInActionHash.store(0, std::memory_order_relaxed);

    uintptr_t plugin = 0;
    uintptr_t actionParams = 0;
    uintptr_t graphManager = 0;
    VehicleSeatTrace::Snapshot snapshot = {};
    if (!rideOn || !VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin) ||
        !VehicleSeatTrace::CaptureSnapshot(plugin, snapshot) ||
        snapshot.rideOn != rideOn ||
        !VehicleSeatTrace::ReadValue(rideOn + 0x98, actionParams) ||
        !actionParams ||
        !VehicleSeatTrace::ReadValue(actionParams + 0x8A8, graphManager) ||
        !graphManager) {
        return;
    }

    g_plugin.store(plugin, std::memory_order_relaxed);
    g_graphManager.store(graphManager, std::memory_order_relaxed);
    g_deadline.store(
        GetTickCount64() + kSessionWindowMs, std::memory_order_relaxed);
    g_rideOn.store(rideOn, std::memory_order_release);
    g_sessionId.fetch_add(1, std::memory_order_acq_rel);
}

void ObserveProcessAttach(uintptr_t rideOn)
{
    if (rideOn != g_rideOn.load(std::memory_order_acquire) || !InWindow())
        return;
    VehicleSeatTrace::Snapshot snapshot = {};
    if (ReadLiveSnapshot(snapshot) && snapshot.current == 1 &&
        snapshot.next == 1 && snapshot.stage == 2 &&
        snapshot.b189 && snapshot.b18A && snapshot.b191) {
        g_ready.store(true, std::memory_order_release);
    }
}

void ObserveDriveEnter(uintptr_t driveState)
{
    uintptr_t plugin = 0;
    if (InWindow() &&
        VehicleSeatTrace::ReadValue(driveState + 0x88, plugin) &&
        plugin == g_plugin.load(std::memory_order_relaxed)) {
        g_driveEntered.store(true, std::memory_order_release);
    }
}

bool ObservePostDrivePoseApplied(uintptr_t player)
{
    if (!player || !InWindow() ||
        !g_driveEntered.load(std::memory_order_acquire)) {
        return false;
    }
    const uintptr_t rideOn = g_rideOn.load(std::memory_order_acquire);
    uintptr_t expectedPlayer = 0;
    std::array<float, 9> basis = {};
    if (!rideOn ||
        !VehicleSeatTrace::ReadValue(rideOn + 0x98, expectedPlayer) ||
        expectedPlayer != player ||
        !VehicleSeatTrace::ReadValue(player + 0x100, basis) ||
        !std::isfinite(basis[8]) || basis[8] < 0.9f) {
        return false;
    }
    return !g_postDrivePoseReady.exchange(true, std::memory_order_acq_rel);
}

uintptr_t EnterRideOnUpdate(uintptr_t rideOn)
{
    const uintptr_t previous = t_rideOnUpdate;
    t_rideOnUpdate = rideOn;
    return previous;
}

void LeaveRideOnUpdate(uintptr_t previousRideOn)
{
    t_rideOnUpdate = previousRideOn;
}

bool ObserveRideOnUpdateComplete(uintptr_t rideOn)
{
    if (rideOn != g_rideOn.load(std::memory_order_acquire) || !InWindow() ||
        !g_nativeCompletionSeen.load(std::memory_order_acquire)) {
        return false;
    }
    return !g_nativeCompletionUpdateReturned.exchange(
        true, std::memory_order_acq_rel);
}

bool MatchesGraphEvent(uintptr_t manager, uint32_t eventId)
{
    if (eventId != kBoardingCompleteEvent || !manager ||
        t_rideOnUpdate != g_rideOn.load(std::memory_order_relaxed) ||
        manager != g_graphManager.load(std::memory_order_relaxed) ||
        !AllComponentsReady() ||
        !g_ready.load(std::memory_order_acquire)) {
        return false;
    }
    VehicleSeatTrace::Snapshot snapshot = {};
    return ReadLiveSnapshot(snapshot) && snapshot.current == 1 &&
        snapshot.next == 1 && snapshot.stage == 2 && snapshot.b18B;
}

bool CompletionLayersReady(bool nativeResult)
{
    if (nativeResult)
        g_nativeCompletionSeen.store(true, std::memory_order_release);
    const bool fastLayersReady =
        g_animationCompleted.load(std::memory_order_acquire) &&
        g_nativeCompletionSeen.load(std::memory_order_acquire);
    const bool timedOutNative =
        GetTickCount64() > g_deadline.load(std::memory_order_relaxed) &&
        g_nativeCompletionSeen.load(std::memory_order_acquire);
    return (fastLayersReady || timedOutNative) &&
        !g_graphForced.load(std::memory_order_relaxed);
}

bool CanFastForwardAnimation()
{
    if (!AllComponentsReady() ||
        !g_ready.load(std::memory_order_acquire) || !InWindow()) {
        return false;
    }
    VehicleSeatTrace::Snapshot snapshot = {};
    return ReadLiveSnapshot(snapshot) && snapshot.current == 1 &&
        snapshot.next == 1 && snapshot.stage == 2;
}

bool ShouldFastForwardCutIn(uintptr_t cutInCamera)
{
    return cutInCamera && IsReadyRideOn() &&
        g_nativeCompletionUpdateReturned.load(std::memory_order_acquire) &&
        g_driveEntered.load(std::memory_order_acquire) &&
        g_postDrivePoseReady.load(std::memory_order_acquire) &&
        !g_cutInAttempted.load(std::memory_order_relaxed);
}

bool IsActiveCutInSession(uintptr_t cutInCamera, uint32_t actionHash)
{
    return cutInCamera && IsReadyRideOn() &&
        cutInCamera == g_cutInCamera.load(std::memory_order_relaxed) &&
        actionHash != 0 &&
        actionHash == g_cutInActionHash.load(std::memory_order_acquire);
}

bool MarkGraphEventForced()
{
    return !g_graphForced.exchange(true, std::memory_order_acq_rel);
}

bool MarkCutInFastForwarded(uintptr_t cutInCamera, uint32_t actionHash)
{
    if (g_cutInAttempted.exchange(true, std::memory_order_acq_rel))
        return false;
    g_cutInCamera.store(cutInCamera, std::memory_order_relaxed);
    g_cutInActionHash.store(actionHash, std::memory_order_release);
    return true;
}

void ConfirmAnimationFastForwarded()
{
    g_animationCompleted.store(true, std::memory_order_release);
}

uint32_t CurrentSessionId()
{
    return g_sessionId.load(std::memory_order_acquire);
}

uintptr_t ActiveRideOn()
{
    return InWindow() ? g_rideOn.load(std::memory_order_acquire) : 0;
}

} // namespace FastBoardingSession
