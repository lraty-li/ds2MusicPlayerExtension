#include "pch.h"
#include "RideOffGraphEndpoint.h"

#include "PatternScan.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>

namespace RideOffGraphEndpoint {
namespace {

constexpr const char* kRideOffCallSignature =
    "41 80 7C 24 22 00 75 0C 41 80 7C 24 13 00 0F 84 ? ? ? ? "
    "49 8D B4 24 20 12 00 00 48 8B 84 24 ? ? ? ? "
    "48 8B 90 00 2A 00 00 C6 44 24 20 01 F3 0F 10 1D ? ? ? ? "
    "48 89 F1 45 31 C0 FF 15 ? ? ? ?";
constexpr uintptr_t kCallOffset = 0x3E;
constexpr uintptr_t kCallSize = 6;
constexpr bool kEnableEndpointMutation = true;
constexpr double kMaximumAdvanceSeconds = 0.25;
constexpr double kNativeCompletionLeadSeconds = 0.1;

std::atomic<uintptr_t> g_callerReturn{0};
SRWLOCK g_traceLock = SRWLOCK_INIT;
uint32_t g_traceSession = 0;
uintptr_t g_traceDescriptor = 0;
int32_t g_traceBucket = -1;
bool g_traceReachedEnd = false;
thread_local uint32_t t_queueClockSession = 0;
thread_local float t_queueClockExtra = 0.0f;

struct InputState {
    uintptr_t timeState = 0;
    float mirrorEnd = 0.0f;
    uint8_t rangeMode = 0;
    double end = 0.0;
    double start = 0.0;
    float duration = 0.0f;
};

struct OutputState {
    uintptr_t timeState = 0;
    float duration = 0.0f;
    float evaluationEnd = 0.0f;
    uint8_t reachedEnd = 0;
};

bool ReadInput(
    uintptr_t output, uintptr_t descriptor, InputState& state)
{
    return output && descriptor &&
        VehicleSeatTrace::ReadValue(output + 0x8, state.timeState) &&
        state.timeState &&
        VehicleSeatTrace::ReadValue(
            state.timeState, state.mirrorEnd) &&
        VehicleSeatTrace::ReadValue(
            state.timeState + 0xC, state.rangeMode) &&
        VehicleSeatTrace::ReadValue(state.timeState + 0x10, state.end) &&
        VehicleSeatTrace::ReadValue(state.timeState + 0x18, state.start) &&
        VehicleSeatTrace::ReadValue(descriptor + 0x3C, state.duration);
}

bool ReadOutput(uintptr_t output, OutputState& state)
{
    if (!output ||
        !VehicleSeatTrace::ReadValue(output + 0x8, state.timeState) ||
        !state.timeState ||
        !VehicleSeatTrace::ReadValue(output + 0x48, state.duration) ||
        !VehicleSeatTrace::ReadValue(
            output + 0x4C, state.evaluationEnd)) {
        return false;
    }
    return VehicleSeatTrace::ReadValue(
        state.timeState + 0xE, state.reachedEnd);
}

bool IsEligibleInput(const InputState& state)
{
    return state.rangeMode == 0 &&
        std::isfinite(state.mirrorEnd) && std::isfinite(state.end) &&
        std::isfinite(state.start) && std::isfinite(state.duration) &&
        state.start >= 0.0 && state.end >= state.start &&
        state.duration > 1.0f && state.duration < 5.0f &&
        static_cast<double>(state.duration) > state.end;
}

} // namespace

uintptr_t FindCallerReturn(
    uintptr_t textStart, size_t textSize, uintptr_t evaluatorSlot)
{
    const uintptr_t match = PatternScan::FindUnique(
        textStart, textSize, kRideOffCallSignature);
    if (!match)
        return 0;
    const uintptr_t call = match + kCallOffset;
    const auto* opcode = reinterpret_cast<const uint8_t*>(call);
    if (call + kCallSize > textStart + textSize ||
        opcode[0] != 0xFF || opcode[1] != 0x15) {
        return 0;
    }
    if (PatternScan::ResolveRip(call, 2) != evaluatorSlot)
        return 0;
    return call + kCallSize;
}

void Enable(uintptr_t callerReturn, const Logger& logger)
{
    if (!callerReturn)
        return;
    g_callerReturn.store(callerReturn, std::memory_order_release);
    RideOffSession::ReportComponentReady(
        RideOffSession::kGraphEndpointComponent);
    logger.Log("FastRideOff graph endpoint ready caller=" +
        VehicleSeatTrace::Hex(callerReturn) + " mutation=1");
}

void Prepare(
    const Logger& logger, uintptr_t caller, uintptr_t output,
    uintptr_t descriptor, uint8_t mode, float timeScale,
    uint8_t evaluatePose)
{
    if (caller != g_callerReturn.load(std::memory_order_acquire) ||
        mode != 0 || timeScale != 1.0f || evaluatePose != 1)
        return;

    InputState input = {};
    if (!ReadInput(output, descriptor, input) || !IsEligibleInput(input))
        return;

    if (!kEnableEndpointMutation) {
        const uint32_t session = RideOffSession::ActiveId();
        if (!session)
            return;
        bool selected = false;
        AcquireSRWLockExclusive(&g_traceLock);
        if (g_traceSession != session) {
            g_traceSession = session;
            g_traceDescriptor = descriptor;
            g_traceBucket = -1;
            g_traceReachedEnd = false;
            selected = true;
        }
        ReleaseSRWLockExclusive(&g_traceLock);
        if (selected) {
            std::ostringstream oss;
            oss << "RideOff natural endpoint selected"
                << " session=" << session
                << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
                << " interval=" << input.start << ".." << input.end
                << " duration=" << input.duration;
            logger.Log(oss.str());
        }
        return;
    }

    uint32_t session = 0;
    if (!RideOffSession::TryClaimGraphEndpoint(
            output, descriptor, session) &&
        !RideOffSession::IsClaimedGraphEndpoint(
            output, descriptor, session)) {
        return;
    }
    const double nativeHandoffEnd =
        static_cast<double>(input.duration) -
        kNativeCompletionLeadSeconds;
    if (input.end >= nativeHandoffEnd)
        return;
    const double steppedEnd = (std::min)(
        input.end + kMaximumAdvanceSeconds, nativeHandoffEnd);
    const float requestedEnd = static_cast<float>(steppedEnd);
    const double requestedEndDouble = static_cast<double>(requestedEnd);
    const bool written = std::isfinite(requestedEnd) &&
        requestedEndDouble > input.end &&
        VehicleSeatTrace::WriteValue<double>(
            input.timeState + 0x10, requestedEndDouble) &&
        VehicleSeatTrace::WriteValue<float>(
            input.timeState, requestedEnd);
    if (!written) {
        VehicleSeatTrace::WriteValue<double>(
            input.timeState + 0x10, input.end);
        VehicleSeatTrace::WriteValue<float>(
            input.timeState, input.mirrorEnd);
        RideOffSession::ReleaseGraphEndpointClaim(
            output, descriptor, session);
        return;
    }
    if (t_queueClockSession != session) {
        t_queueClockSession = session;
        t_queueClockExtra = 0.0f;
    }
    t_queueClockExtra += static_cast<float>(
        static_cast<double>(requestedEnd) - input.end);

    std::ostringstream oss;
    oss << "FastRideOff graph endpoint prepared"
        << " session=" << session
        << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
        << " interval=" << input.start << ".." << input.end
        << "->" << requestedEnd
        << " duration=" << input.duration;
    logger.Log(oss.str());
}

void ObserveResult(
    const Logger& logger, uintptr_t caller, uintptr_t output,
    uintptr_t descriptor)
{
    if (caller != g_callerReturn.load(std::memory_order_acquire))
        return;

    if (!kEnableEndpointMutation) {
        const uint32_t session = RideOffSession::ActiveId();
        OutputState result = {};
        if (!session || !ReadOutput(output, result) ||
            !std::isfinite(result.duration) ||
            !std::isfinite(result.evaluationEnd) || result.duration <= 0.0f) {
            return;
        }
        int32_t bucket = static_cast<int32_t>(
            std::floor(result.evaluationEnd * 8.0f / result.duration));
        if (bucket < 0)
            bucket = 0;
        if (bucket > 8)
            bucket = 8;
        bool log = false;
        AcquireSRWLockExclusive(&g_traceLock);
        if (g_traceSession == session &&
            g_traceDescriptor == descriptor &&
            (bucket > g_traceBucket ||
                static_cast<bool>(result.reachedEnd) != g_traceReachedEnd)) {
            g_traceBucket = bucket;
            g_traceReachedEnd = result.reachedEnd != 0;
            log = true;
        }
        ReleaseSRWLockExclusive(&g_traceLock);
        if (log) {
            std::ostringstream oss;
            oss << "RideOff natural endpoint progress"
                << " session=" << session
                << " elapsedMs=" << RideOffSession::ElapsedMs()
                << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
                << " duration=" << result.duration
                << " evaluationEnd=" << result.evaluationEnd
                << " reachedEnd=" << static_cast<uint32_t>(result.reachedEnd);
            logger.Log(oss.str());
        }
        return;
    }
    uint32_t session = 0;
    if (!RideOffSession::IsClaimedGraphEndpoint(
            output, descriptor, session)) {
        return;
    }

    OutputState result = {};
    if (!ReadOutput(output, result) || !result.reachedEnd ||
        !std::isfinite(result.duration) ||
        !std::isfinite(result.evaluationEnd) ||
        result.duration <= 1.0f ||
        result.evaluationEnd < result.duration ||
        !RideOffSession::MarkGraphEndpointComplete(
            output, descriptor, session)) {
        return;
    }

    std::ostringstream oss;
    oss << "FastRideOff graph endpoint complete"
        << " session=" << session
        << " duration=" << result.duration
        << " evaluationEnd=" << result.evaluationEnd
        << " reachedEnd=" << static_cast<uint32_t>(result.reachedEnd);
    logger.Log(oss.str());
}

bool HasPendingQueueClockAdvance()
{
    return t_queueClockSession != 0 && t_queueClockExtra > 0.0f;
}

bool TakePendingQueueClockAdvance(
    uint32_t& session, float& extraSeconds)
{
    session = t_queueClockSession;
    extraSeconds = t_queueClockExtra;
    if (!session || session != RideOffSession::ActiveId() ||
        !std::isfinite(extraSeconds) || extraSeconds <= 0.0f) {
        return false;
    }
    t_queueClockSession = 0;
    t_queueClockExtra = 0.0f;
    return true;
}

} // namespace RideOffGraphEndpoint
