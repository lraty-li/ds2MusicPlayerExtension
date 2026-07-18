#include "pch.h"
#include "FullGameDescriptorMode.h"

#include "DescriptorNeighborhoodTrace.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace FullGameDescriptorMode {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kBoardingDescriptorCallSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE BB 07 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 95 30 27 00 00 C6 44 24 20 01 "
    "48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr const char* kSecondaryBoardingCallSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE CE B8 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 84 24 ? ? ? ? 48 8B 8C 24 ? ? ? ? "
    "4C 8D 2C 08 49 83 C5 38 48 8B 95 78 34 00 00 C6 44 24 20 01 "
    "4C 89 E9 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr uint32_t kIndirectCallOffset = 0x34;
constexpr uint32_t kSecondaryIndirectCallOffset = 0x47;
constexpr uint32_t kCallInstructionSize = 6;

using EvaluateDescriptorFn = void(__fastcall*)(
    uintptr_t output, uintptr_t descriptor, uint8_t mode, float weight);

std::atomic<bool> g_started{false};
std::atomic<uint32_t> g_session{0};
std::atomic<uint32_t> g_lastBucket{UINT32_MAX};
std::atomic<uint64_t> g_sessionStartTick{0};
std::atomic<uint64_t> g_lastBoardingCallTick{0};
std::atomic<bool> g_reportedInactive{true};
HMODULE g_fullGame = nullptr;
const Logger* g_logger = nullptr;
uintptr_t g_boardingReturn = 0;
uintptr_t g_secondaryBoardingReturn = 0;
EvaluateDescriptorFn g_original = nullptr;
thread_local uint32_t t_boardingFrame = UINT32_MAX;
thread_local uint64_t t_boardingTick = 0;

struct ResultSnapshot {
    uintptr_t syncState = 0;
    uintptr_t single = 0;
    uint32_t frame = 0;
    uint8_t usesExplicitRange = 0;
    uint8_t syncFlagD = 0;
    uint8_t reachedEnd = 0;
    double rangeStart = 0.0;
    double rangeEnd = 0.0;
    float duration = 0.0f;
    float syncDuration = 0.0f;
    float overrideValue = 0.0f;
};

ResultSnapshot ReadResult(uintptr_t output)
{
    ResultSnapshot result = {};
    VehicleSeatTrace::ReadValue(output + 0x8, result.syncState);
    VehicleSeatTrace::ReadValue(output + 0x40, result.single);
    VehicleSeatTrace::ReadValue(output + 0x48, result.duration);
    VehicleSeatTrace::ReadValue(output + 0x4C, result.syncDuration);
    VehicleSeatTrace::ReadValue(output + 0x50, result.overrideValue);
    if (result.syncState) {
        VehicleSeatTrace::ReadValue(result.syncState + 0x8, result.frame);
        VehicleSeatTrace::ReadValue(
            result.syncState + 0xC, result.usesExplicitRange);
        VehicleSeatTrace::ReadValue(result.syncState + 0xD, result.syncFlagD);
        VehicleSeatTrace::ReadValue(result.syncState + 0xE, result.reachedEnd);
        VehicleSeatTrace::ReadValue(result.syncState + 0x10, result.rangeStart);
        VehicleSeatTrace::ReadValue(result.syncState + 0x18, result.rangeEnd);
    }
    return result;
}

void AppendResult(
    std::ostringstream& oss, const char* label, const ResultSnapshot& result)
{
    oss << ' ' << label << "State=" << VehicleSeatTrace::Hex(result.syncState)
        << ' ' << label << "Frame=" << result.frame
        << ' ' << label << "RangeMode="
        << static_cast<uint32_t>(result.usesExplicitRange)
        << ' ' << label << "FlagD=" << static_cast<uint32_t>(result.syncFlagD)
        << ' ' << label << "End=" << static_cast<uint32_t>(result.reachedEnd)
        << ' ' << label << "Range=" << result.rangeStart << ".." << result.rangeEnd
        << ' ' << label << "Single=" << VehicleSeatTrace::Hex(result.single)
        << ' ' << label << "Duration=" << result.duration
        << ' ' << label << "SyncDuration=" << result.syncDuration
        << ' ' << label << "Override=" << result.overrideValue;
}

uint32_t BeginOrContinueSession(uint64_t now)
{
    const uint64_t previous = g_lastBoardingCallTick.exchange(now);
    if (previous && now - previous <= 1000)
        return g_session.load();

    const uint32_t session = g_session.fetch_add(1) + 1;
    g_sessionStartTick.store(now);
    g_lastBucket.store(UINT32_MAX);
    g_reportedInactive.store(false);
    return session;
}

bool IsExecutableAddress(uintptr_t address)
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

void __fastcall HookEvaluateDescriptor(
    uintptr_t output, uintptr_t descriptor, uint8_t mode, float weight)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (caller != g_boardingReturn) {
        g_original(output, descriptor, mode, weight);
        const uint64_t now = GetTickCount64();
        if (DescriptorNeighborhoodTrace::Active()) {
            const ResultSnapshot after = ReadResult(output);
            const bool sameEvaluationWindow =
                t_boardingFrame != UINT32_MAX &&
                now - t_boardingTick <= 1;
            const uint64_t lastBoarding = g_lastBoardingCallTick.load();
            const bool postBoardingResult = lastBoarding &&
                now - lastBoarding > 20 && now - lastBoarding <= 1000 &&
                after.single && after.duration > 0.0f;
            if (caller == g_secondaryBoardingReturn ||
                sameEvaluationWindow || postBoardingResult) {
                DescriptorNeighborhoodTrace::Observe(
                    caller, descriptor, mode, weight, after.frame,
                    after.single, after.duration, after.syncDuration);
            }
        }
        return;
    }

    const uint64_t now = GetTickCount64();
    const uint32_t session = BeginOrContinueSession(now);
    const ResultSnapshot before = ReadResult(output);
    g_original(output, descriptor, mode, weight);
    const ResultSnapshot after = ReadResult(output);
    t_boardingFrame = after.frame;
    t_boardingTick = now;
    DescriptorNeighborhoodTrace::MarkBoarding(
        session, now, after.syncState);
    DescriptorNeighborhoodTrace::Observe(
        caller, descriptor, mode, weight, after.frame,
        after.single, after.duration, after.syncDuration);
    const uint64_t elapsedMs = now - g_sessionStartTick.load();
    const uint32_t bucket = static_cast<uint32_t>(elapsedMs / 250);
    if (g_lastBucket.exchange(bucket) == bucket)
        return;

    std::ostringstream oss;
    oss << "BoardingDescriptor session=" << session
        << " elapsedMs=" << elapsedMs
        << " callerRva=" << VehicleSeatTrace::Hex(
            caller - reinterpret_cast<uintptr_t>(g_fullGame))
        << " mode=" << static_cast<uint32_t>(mode)
        << " weight=" << weight
        << " descriptor=" << VehicleSeatTrace::Hex(descriptor);
    AppendResult(oss, "before", before);
    AppendResult(oss, "after", after);
    g_logger->Log(oss.str());
}

void MonitorBoardingSessions()
{
    for (;;) {
        Sleep(50);
        const uint64_t now = GetTickCount64();
        DescriptorNeighborhoodTrace::FlushIfReady(now);
        const uint64_t lastCall = g_lastBoardingCallTick.load();
        if (!lastCall || g_reportedInactive.load())
            continue;
        if (now - lastCall < 250 || g_reportedInactive.exchange(true))
            continue;

        std::ostringstream oss;
        oss << "BoardingDescriptor inactive session=" << g_session.load()
            << " activeMs=" << lastCall - g_sessionStartTick.load();
        g_logger->Log(oss.str());
    }
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
    const uintptr_t callBranch = PatternScan::FindUnique(
        textStart, textSize, kBoardingDescriptorCallSignature);
    const uintptr_t secondaryBranch = PatternScan::FindUnique(
        textStart, textSize, kSecondaryBoardingCallSignature);
    if (!callBranch || !secondaryBranch) {
        g_logger->Log("Boarding descriptor call signatures not unique");
        return false;
    }

    const uintptr_t callInstruction = callBranch + kIndirectCallOffset;
    const uintptr_t slotAddress = PatternScan::ResolveRip(callInstruction, 2);
    const uintptr_t secondaryCall =
        secondaryBranch + kSecondaryIndirectCallOffset;
    if (PatternScan::ResolveRip(secondaryCall, 2) != slotAddress) {
        g_logger->Log("Boarding descriptor evaluator slots disagree");
        return false;
    }
    auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
    void* original = *slot;
    if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
        g_logger->Log("Boarding descriptor evaluator slot is not ready");
        return false;
    }

    g_boardingReturn = callInstruction + kCallInstructionSize;
    g_secondaryBoardingReturn = secondaryCall + kCallInstructionSize;
    g_original = reinterpret_cast<EvaluateDescriptorFn>(original);
    DescriptorNeighborhoodTrace::Initialize(
        *g_logger, reinterpret_cast<uintptr_t>(g_fullGame));
    void* replaced = InterlockedExchangePointer(
        slot, reinterpret_cast<void*>(&HookEvaluateDescriptor));
    if (replaced != original) {
        InterlockedExchangePointer(slot, replaced);
        g_logger->Log("Boarding descriptor evaluator slot changed during install");
        return false;
    }
    g_logger->Log("FullGame descriptor pointer hook installed slot=" +
        VehicleSeatTrace::Hex(slotAddress) +
        " target=" + VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(original)));
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded()) {
        g_logger->Log("Boarding descriptor mode deferred install failed");
        return 0;
    }
    MonitorBoardingSessions();
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
    if (!thread)
        return false;
    CloseHandle(thread);
    logger.Log("FullGame descriptor pointer observer deferred install started");
    return true;
}

} // namespace FullGameDescriptorMode
