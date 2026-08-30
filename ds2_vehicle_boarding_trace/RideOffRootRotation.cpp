#include "pch.h"
#include "RideOffRootRotation.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sstream>

namespace RideOffRootRotation {
namespace {

constexpr char kComposeRootRotationSignature[] =
    "48 8B C4 55 53 57 41 56 48 8D A8 ? ? ? ? "
    "48 81 EC A8 01 00 00";
constexpr size_t kPatchLength = 15;

using ComposeRootRotationFn = float*(__fastcall*)(
    uintptr_t mover, float* outputEuler, float frameDelta);

std::atomic<bool> g_started{false};
std::atomic<uint32_t> g_leveledSession{0};
const Logger* g_logger = nullptr;
ComposeRootRotationFn g_original = nullptr;
thread_local uintptr_t t_recoveryMover = 0;

void LogLeveledRotation(
    uint32_t session, uintptr_t mover, const float* euler)
{
    std::ostringstream oss;
    oss << "FastRideOff first Basic root rotation leveled"
        << " session=" << session
        << " elapsedMs=" << RideOffSession::ElapsedMs()
        << " mover=" << VehicleSeatTrace::Hex(mover)
        << " raw=" << euler[0] << ',' << euler[1]
        << ',' << euler[2] << ',' << euler[3];
    g_logger->Log(oss.str());
}

float* __fastcall HookComposeRootRotation(
    uintptr_t mover, float* outputEuler, float frameDelta)
{
    float* result = g_original(mover, outputEuler, frameDelta);
    if (!outputEuler || !mover || mover != t_recoveryMover)
        return result;

    const uint32_t session = RideOffSession::ActiveId();
    if (!session ||
        g_leveledSession.exchange(
            session, std::memory_order_acq_rel) == session) {
        return result;
    }

    const float rawEuler[4] = {
        outputEuler[0], outputEuler[1],
        outputEuler[2], outputEuler[3]};
    outputEuler[0] = 0.0f;
    outputEuler[1] = 0.0f;
    LogLeveledRotation(session, mover, rawEuler);
    return result;
}

} // namespace

uintptr_t EnterRecoveryPose(uintptr_t moverAccessor)
{
    const uintptr_t previousMover = t_recoveryMover;
    t_recoveryMover = 0;
    const uint32_t session = RideOffSession::ActiveId();
    uint32_t animationState = UINT32_MAX;
    if (!session || !RideOffSession::CompletionReady() ||
        !RideOffSession::MatchesMoverAccessor(moverAccessor) ||
        !VehicleSeatTrace::ReadValue(
            moverAccessor + 0x8, t_recoveryMover) ||
        !t_recoveryMover ||
        !VehicleSeatTrace::ReadValue(
            t_recoveryMover + 0x2E0, animationState) ||
        animationState != 1) {
        t_recoveryMover = 0;
    }
    return previousMover;
}

void LeaveRecoveryPose(uintptr_t previousMover)
{
    t_recoveryMover = previousMover;
}

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
        textStart, textSize, kComposeRootRotationSignature);
    if (!target) {
        logger.Log(
            "FastRideOff root rotation signature missing or ambiguous");
        return false;
    }

    void* trampoline = JumpHook::MakeTrampoline(target, kPatchLength);
    if (!trampoline)
        return false;
    g_logger = &logger;
    g_original = reinterpret_cast<ComposeRootRotationFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookComposeRootRotation),
            kPatchLength)) {
        return false;
    }

    g_started.store(true, std::memory_order_release);
    logger.Log("FastRideOff Basic root rotation hook installed at " +
        VehicleSeatTrace::Hex(target));
    return true;
}

} // namespace RideOffRootRotation
