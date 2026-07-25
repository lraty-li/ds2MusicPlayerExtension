#include "pch.h"
#include "FullGameBoardingFastForward.h"

#include "FastBoardingSession.h"
#include "PatternScan.h"
#include "RideOffDescriptorTrace.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>

namespace FullGameBoardingFastForward {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kBoardingCallSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE BB 07 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 95 30 27 00 00 C6 44 24 20 01 "
    "48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr const char* kSecondaryCallSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE CE B8 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 84 24 ? ? ? ? 48 8B 8C 24 ? ? ? ? "
    "4C 8D 2C 08 49 83 C5 38 48 8B 95 78 34 00 00 C6 44 24 20 01 "
    "4C 89 E9 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr const char* kApproach2CandidateASignature =
    "41 80 BE E1 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 20 27 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?";
constexpr const char* kApproach1Side0Signature =
    "41 80 BE BA 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 18 27 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?";
constexpr const char* kApproach2CandidateCSignature =
    "41 80 BE E2 07 05 00 00 0F 84 ? ? ? ? 48 8B 95 B8 26 00 00 "
    "C6 44 24 20 01 48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 "
    "FF 15 ? ? ? ?";
constexpr uint32_t kPrimaryCallOffset = 0x34;
constexpr uint32_t kSecondaryCallOffset = 0x47;
constexpr uint32_t kApproachCallOffset = 0x29;
constexpr uint32_t kIndirectCallSize = 6;
constexpr float kFastTimeScale = 512.0f;

using EvaluateDescriptorFn = void(__fastcall*)(
    uintptr_t output, uintptr_t descriptor, uint8_t mode,
    float timeScale, uint8_t evaluatePose);

std::atomic<bool> g_started{false};
HMODULE g_fullGame = nullptr;
const Logger* g_logger = nullptr;
std::array<uintptr_t, 4> g_boardingReturns{};
EvaluateDescriptorFn g_original = nullptr;
SRWLOCK g_leafLock = SRWLOCK_INIT;
uint32_t g_leafSession = 0;
uint32_t g_activeLeaf = 0;
uintptr_t g_activeDescriptor = 0;
bool g_leafCompleted = false;

struct ResultState {
    uintptr_t single = 0;
    uintptr_t syncState = 0;
    float duration = 0.0f;
    float syncDuration = 0.0f;
    uint8_t reachedEnd = 0;
};

bool ReadResult(uintptr_t output, ResultState& result)
{
    if (!VehicleSeatTrace::ReadValue(output + 0x8, result.syncState) ||
        !VehicleSeatTrace::ReadValue(output + 0x40, result.single) ||
        !VehicleSeatTrace::ReadValue(output + 0x48, result.duration) ||
        !VehicleSeatTrace::ReadValue(
            output + 0x4C, result.syncDuration)) {
        return false;
    }
    if (result.syncState) {
        VehicleSeatTrace::ReadValue(
            result.syncState + 0xE, result.reachedEnd);
    }
    return true;
}

bool IsFastResultComplete(const ResultState& result)
{
    return result.single && result.duration > 0.0f &&
        result.duration < 0.1f &&
        (result.reachedEnd || result.syncDuration >= result.duration);
}

uint32_t LeafMask(uintptr_t caller)
{
    for (uint32_t index = 0; index < g_boardingReturns.size(); ++index) {
        if (caller == g_boardingReturns[index])
            return 1u << index;
    }
    return 0;
}

bool ClaimLeafForSession(
    uint32_t leaf, uintptr_t descriptor, uint32_t& claimedSession)
{
    const uint32_t session = FastBoardingSession::CurrentSessionId();
    if (!session || !descriptor)
        return false;
    AcquireSRWLockExclusive(&g_leafLock);
    if (g_leafSession != session) {
        g_leafSession = session;
        g_activeLeaf = 0;
        g_activeDescriptor = 0;
        g_leafCompleted = false;
    }
    if (!g_activeDescriptor) {
        g_activeLeaf = leaf;
        g_activeDescriptor = descriptor;
    }
    const bool claimed = !g_leafCompleted &&
        g_activeLeaf == leaf && g_activeDescriptor == descriptor;
    if (claimed)
        claimedSession = session;
    ReleaseSRWLockExclusive(&g_leafLock);
    return claimed;
}

bool CompleteLeafForSession(
    uint32_t session, uint32_t leaf, uintptr_t descriptor)
{
    AcquireSRWLockExclusive(&g_leafLock);
    const bool matched = !g_leafCompleted && g_leafSession == session &&
        g_activeLeaf == leaf && g_activeDescriptor == descriptor;
    if (matched)
        g_leafCompleted = true;
    ReleaseSRWLockExclusive(&g_leafLock);
    return matched;
}

void __fastcall HookEvaluateDescriptor(
    uintptr_t output, uintptr_t descriptor, uint8_t mode,
    float timeScale, uint8_t evaluatePose)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t leaf = LeafMask(caller);
    const uint32_t rideOffSession = RideOffSession::CurrentId();
    uint32_t claimedSession = 0;
    const bool eligible = leaf && mode == 0 && timeScale == 1.0f &&
        evaluatePose == 1 && FastBoardingSession::CanFastForwardAnimation() &&
        ClaimLeafForSession(leaf, descriptor, claimedSession);
    const float effectiveScale = eligible ? kFastTimeScale : timeScale;
    g_original(output, descriptor, mode, effectiveScale, evaluatePose);
    if (rideOffSession) {
        ResultState observed = {};
        ReadResult(output, observed);
        RideOffDescriptorTrace::Observe(
            *g_logger, rideOffSession, reinterpret_cast<uintptr_t>(g_fullGame),
            caller, descriptor, mode, timeScale, evaluatePose, observed.duration,
            observed.syncDuration, observed.reachedEnd);
    }
    if (!eligible)
        return;

    ResultState result = {};
    const bool complete = ReadResult(output, result) &&
        IsFastResultComplete(result);
    std::ostringstream oss;
    oss << "FastBoarding descriptor evaluated"
        << " session=" << claimedSession
        << " leaf=" << leaf
        << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
        << " callerRva=" << VehicleSeatTrace::Hex(
            caller - reinterpret_cast<uintptr_t>(g_fullGame))
        << " scale=" << timeScale << "->" << effectiveScale
        << " mode=" << static_cast<uint32_t>(mode)
        << " pose=" << static_cast<uint32_t>(evaluatePose)
        << " duration=" << result.duration
        << " sync=" << result.syncDuration
        << " end=" << static_cast<uint32_t>(result.reachedEnd)
        << " complete=" << static_cast<uint32_t>(complete);
    g_logger->Log(oss.str());
    if (complete && CompleteLeafForSession(
            claimedSession, leaf, descriptor)) {
        FastBoardingSession::ConfirmAnimationFastForwarded();
    }
}

bool IsExecutable(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION memory = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)))
        return false;
    const DWORD protect = memory.Protect & 0xFF;
    return memory.State == MEM_COMMIT &&
        (protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE ||
         protect == PAGE_EXECUTE_WRITECOPY);
}

bool InstallWhenLoaded()
{
    for (uint32_t attempt = 0; attempt < 450; ++attempt) {
        g_fullGame = GetModuleHandleW(kFullGameModuleName);
        if (g_fullGame)
            break;
        Sleep(100);
    }
    if (!g_fullGame)
        return false;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_fullGame, ".text", textStart, textSize))
        return false;
    const uintptr_t primary = PatternScan::FindUnique(
        textStart, textSize, kBoardingCallSignature);
    const uintptr_t secondary = PatternScan::FindUnique(
        textStart, textSize, kSecondaryCallSignature);
    const std::array<uintptr_t, 3> approachBranches = {
        PatternScan::FindUnique(
            textStart, textSize, kApproach2CandidateASignature),
        PatternScan::FindUnique(
            textStart, textSize, kApproach1Side0Signature),
        PatternScan::FindUnique(
            textStart, textSize, kApproach2CandidateCSignature)};
    if (!primary || !secondary || !approachBranches[0] ||
        !approachBranches[1] || !approachBranches[2])
        return false;

    const uintptr_t call = primary + kPrimaryCallOffset;
    const uintptr_t secondaryCall = secondary + kSecondaryCallOffset;
    const uintptr_t slotAddress = PatternScan::ResolveRip(call, 2);
    if (PatternScan::ResolveRip(secondaryCall, 2) != slotAddress)
        return false;
    g_boardingReturns[0] = call + kIndirectCallSize;
    for (uint32_t index = 0; index < approachBranches.size(); ++index) {
        const uintptr_t approachCall =
            approachBranches[index] + kApproachCallOffset;
        if (PatternScan::ResolveRip(approachCall, 2) != slotAddress)
            return false;
        g_boardingReturns[index + 1] =
            approachCall + kIndirectCallSize;
    }

    auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
    void* original = *slot;
    if (!original || !IsExecutable(reinterpret_cast<uintptr_t>(original)))
        return false;

    g_original = reinterpret_cast<EvaluateDescriptorFn>(original);
    void* replaced = InterlockedCompareExchangePointer(
        slot, reinterpret_cast<void*>(&HookEvaluateDescriptor), original);
    if (replaced != original)
        return false;

    FastBoardingSession::ReportComponentReady(
        FastBoardingSession::kAnimationComponent);
    g_logger->Log("FastBoarding fullgame evaluator wrapper installed slot=" +
        VehicleSeatTrace::Hex(slotAddress) + " leaves=4");
    if (FastBoardingSession::AllComponentsReady())
        g_logger->Log("FastBoarding MOD READY");
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded())
        g_logger->Log("FastBoarding fullgame evaluator install failed");
    return 0;
}

} // namespace

bool TryInstall(const Logger& logger)
{
    if (g_started.exchange(true))
        return true;
    g_logger = &logger;
    HANDLE thread = CreateThread(
        nullptr, 0, DeferredInstallThread, nullptr, 0, nullptr);
    if (!thread) {
        g_started.store(false);
        return false;
    }
    CloseHandle(thread);
    logger.Log("FastBoarding fullgame evaluator deferred install started");
    return true;
}

} // namespace FullGameBoardingFastForward
