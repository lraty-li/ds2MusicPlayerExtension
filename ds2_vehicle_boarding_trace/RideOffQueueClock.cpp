#include "pch.h"
#include "RideOffQueueClock.h"

#include "PatternScan.h"
#include "RideOffGraphEndpoint.h"
#include "RideOffSession.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace RideOffQueueClock {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kRideOffPostEvaluateSignature =
    "48 8B 94 24 80 07 00 00 48 03 94 24 70 07 00 00 "
    "48 8B 8C 24 F0 04 00 00 39 11 0F 85 B4 D0 FF FF "
    "48 8B 84 24 C0 02 00 00 48 8B 94 24 B8 02 00 00 "
    "4C 8D 04 10 49 83 C0 38 0F 28 CE FF 15 ? ? ? ? "
    "48 8B 8C 24 F0 02 00 00";
constexpr uintptr_t kCallOffset = 0x3B;
constexpr uintptr_t kIndirectCallSize = 6;

using PostEvaluateFn = void(__fastcall*)(
    uintptr_t dynamicTable, float deltaSeconds, uintptr_t outputResult);

std::atomic<bool> g_started{false};
const Logger* g_logger = nullptr;
uintptr_t g_rideOffCallerReturn = 0;
PostEvaluateFn g_original = nullptr;

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

void __fastcall HookPostEvaluate(
    uintptr_t dynamicTable, float deltaSeconds, uintptr_t outputResult)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool pending =
        RideOffGraphEndpoint::HasPendingQueueClockAdvance();
    uint32_t session = 0;
    float extraSeconds = 0.0f;
    const bool matched = caller == g_rideOffCallerReturn &&
        std::isfinite(deltaSeconds) && deltaSeconds >= 0.0f &&
        deltaSeconds <= 1.0f &&
        pending &&
        RideOffGraphEndpoint::TakePendingQueueClockAdvance(
            session, extraSeconds) &&
        std::isfinite(extraSeconds) && extraSeconds > 0.0f &&
        extraSeconds <= 1.0f;
    const float effectiveDelta = matched ?
        deltaSeconds + extraSeconds : deltaSeconds;
    g_original(dynamicTable, effectiveDelta, outputResult);
    if (!matched)
        return;

    std::ostringstream oss;
    oss << "FastRideOff queue clock advanced"
        << " session=" << session
        << " elapsedMs=" << RideOffSession::ElapsedMs()
        << " table=" << VehicleSeatTrace::Hex(dynamicTable)
        << " delta=" << deltaSeconds << "+" << extraSeconds
        << "=" << effectiveDelta;
    g_logger->Log(oss.str());
}

bool InstallWhenLoaded()
{
    HMODULE fullGame = nullptr;
    for (uint32_t attempt = 0; attempt < 450; ++attempt) {
        fullGame = GetModuleHandleW(kFullGameModuleName);
        if (fullGame)
            break;
        Sleep(100);
    }
    if (!fullGame)
        return false;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(fullGame, ".text", textStart, textSize))
        return false;
    const uintptr_t match = PatternScan::FindUnique(
        textStart, textSize, kRideOffPostEvaluateSignature);
    if (!match)
        return false;

    const uintptr_t call = match + kCallOffset;
    const auto* opcode = reinterpret_cast<const uint8_t*>(call);
    if (call + kIndirectCallSize > textStart + textSize ||
        opcode[0] != 0xFF || opcode[1] != 0x15) {
        return false;
    }
    const uintptr_t slotAddress = PatternScan::ResolveRip(call, 2);
    if (!slotAddress)
        return false;
    auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
    void* original = *slot;
    if (!original ||
        !IsExecutable(reinterpret_cast<uintptr_t>(original))) {
        return false;
    }

    g_original = reinterpret_cast<PostEvaluateFn>(original);
    g_rideOffCallerReturn = call + kIndirectCallSize;
    void* replaced = InterlockedCompareExchangePointer(
        slot, reinterpret_cast<void*>(&HookPostEvaluate), original);
    if (replaced != original)
        return false;

    RideOffSession::ReportComponentReady(
        RideOffSession::kQueueClockComponent);
    g_logger->Log("FastRideOff dynamic-table clock wrapper installed slot=" +
        VehicleSeatTrace::Hex(slotAddress) + " caller=" +
        VehicleSeatTrace::Hex(g_rideOffCallerReturn));
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded())
        g_logger->Log("FastRideOff dynamic-table clock wrapper install failed");
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
    logger.Log("FastRideOff dynamic-table clock deferred install started");
    return true;
}

} // namespace RideOffQueueClock
