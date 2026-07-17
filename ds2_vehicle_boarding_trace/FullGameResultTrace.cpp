#include "pch.h"
#include "FullGameResultTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "VehicleSnapshot.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>

namespace FullGameResultTrace {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kMergeSignature =
    "56 57 53 48 83 EC ? 44 89 C3 48 89 D6 48 89 CF";
constexpr size_t kPatchLen = 13;

using MergeChannelsFn = uint8_t(__fastcall*)(
    uintptr_t output, uintptr_t input, uint8_t channelMask);

struct ResultChannels {
    uintptr_t countOwner = 0;
    uintptr_t items = 0;
    uintptr_t altItems = 0;
    uintptr_t single = 0;
    uint32_t count = 0;
    float duration = 0.0f;
    float syncDuration = 0.0f;
};

std::atomic<bool> g_started{false};
std::atomic<bool> g_loggedUnsafeReplacement{false};
std::atomic<bool> g_loggedCandidateNeighbors{false};
std::atomic<uint32_t> g_lastDurationBucket{UINT32_MAX};
std::array<std::atomic<uintptr_t>, 128> g_callers{};
HMODULE g_fullGame = nullptr;
const Logger* g_logger = nullptr;
MergeChannelsFn g_original = nullptr;

bool RememberCaller(uintptr_t caller)
{
    for (auto& slot : g_callers) {
        uintptr_t value = slot.load();
        if (value == caller)
            return false;
        if (!value && slot.compare_exchange_strong(value, caller))
            return true;
    }
    return false;
}

ResultChannels ReadChannels(uintptr_t result)
{
    ResultChannels channels = {};
    if (!result)
        return channels;
    VehicleSeatTrace::ReadValue(result, channels.countOwner);
    VehicleSeatTrace::ReadValue(result + 0x10, channels.items);
    VehicleSeatTrace::ReadValue(result + 0x20, channels.altItems);
    VehicleSeatTrace::ReadValue(result + 0x40, channels.single);
    VehicleSeatTrace::ReadValue(result + 0x48, channels.duration);
    VehicleSeatTrace::ReadValue(result + 0x4C, channels.syncDuration);
    if (channels.countOwner)
        VehicleSeatTrace::ReadValue(channels.countOwner, channels.count);
    return channels;
}

void AppendChannels(
    std::ostringstream& oss, const char* label, const ResultChannels& channels)
{
    oss << ' ' << label << "Count=" << channels.count
        << ' ' << label << "Items=" << VehicleSeatTrace::Hex(channels.items)
        << ' ' << label << "Alt=" << VehicleSeatTrace::Hex(channels.altItems)
        << ' ' << label << "Single=" << VehicleSeatTrace::Hex(channels.single)
        << ' ' << label << "Duration=" << channels.duration
        << ' ' << label << "SyncDuration=" << channels.syncDuration;
}

void LogBoardingDurationOwner(
    uintptr_t result, const ResultChannels& channels, float elapsed)
{
    if (!channels.single || channels.duration < 3.5f ||
        channels.duration > 3.6f) {
        return;
    }
    if (!g_loggedCandidateNeighbors.exchange(true)) {
        const ResultChannels before = ReadChannels(result - 0x60);
        const ResultChannels after = ReadChannels(result + 0x60);
        std::ostringstream candidates;
        candidates << "FullGameBoardingCandidateNeighbors selected="
            << VehicleSeatTrace::Hex(result);
        AppendChannels(candidates, "before", before);
        AppendChannels(candidates, "selected", channels);
        AppendChannels(candidates, "after", after);
        g_logger->Log(candidates.str());
    }
    const uint32_t bucket = static_cast<uint32_t>(elapsed * 4.0f);
    if (g_lastDurationBucket.exchange(bucket) == bucket)
        return;

    std::ostringstream oss;
    oss << "FullGameBoardingDurationOwner elapsed=" << elapsed
        << " single=" << VehicleSeatTrace::Hex(channels.single)
        << " duration=" << channels.duration;
    uintptr_t context = 0;
    VehicleSeatTrace::ReadValue(channels.single + 0x8, context);
    oss << " context=" << VehicleSeatTrace::Hex(context);
    for (uint32_t i = 0; i < 12; ++i) {
        uintptr_t value = 0;
        VehicleSeatTrace::ReadValue(
            channels.single + sizeof(value) * i, value);
        oss << " q" << i << '=' << VehicleSeatTrace::Hex(value);
    }
    for (uint32_t i = 0; context && i < 16; ++i) {
        uintptr_t value = 0;
        VehicleSeatTrace::ReadValue(context + sizeof(value) * i, value);
        oss << " c" << i << '=' << VehicleSeatTrace::Hex(value);
    }
    g_logger->Log(oss.str());
}

uint8_t __fastcall HookMergeChannels(
    uintptr_t output, uintptr_t input, uint8_t channelMask)
{
    const uintptr_t rideOn = RideOnEnterInterceptor::ActiveBoardingRideOn();
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    float elapsed = 0.0f;
    ResultChannels inputChannels = {};
    if (rideOn) {
        VehicleSeatTrace::ReadValue(rideOn + 0x180, elapsed);
        inputChannels = ReadChannels(input);
        LogBoardingDurationOwner(input, inputChannels, elapsed);
    }

    const bool isLongBoardingResult =
        RideOnEnterInterceptor::FastBoardingSuppressionActive() &&
        channelMask == 0x73 && inputChannels.count == 1 &&
        inputChannels.single && inputChannels.duration > 3.5f &&
        inputChannels.duration < 3.6f;
    if (isLongBoardingResult &&
        !g_loggedUnsafeReplacement.exchange(true)) {
        std::ostringstream oss;
        oss << "FastBoarding kept complete long result while seeking"
            << " a native seated result"
            << " input=" << VehicleSeatTrace::Hex(input)
            << " output=" << VehicleSeatTrace::Hex(output)
            << " duration=" << inputChannels.duration
            << " elapsed=" << elapsed;
        g_logger->Log(oss.str());
    }

    const uint8_t result = g_original(output, input, channelMask);
    if (!rideOn)
        return result;
    if (!RememberCaller(caller))
        return result;
    const ResultChannels outputChannels = ReadChannels(output);
    std::ostringstream oss;
    oss << "FullGameResultMerge callerRva="
        << VehicleSeatTrace::Hex(caller - reinterpret_cast<uintptr_t>(g_fullGame))
        << " elapsed=" << elapsed
        << " mask=0x" << std::hex << static_cast<uint32_t>(channelMask)
        << std::dec << " return=" << static_cast<uint32_t>(result)
        << " inputResult=" << VehicleSeatTrace::Hex(input)
        << " outputResult=" << VehicleSeatTrace::Hex(output);
    AppendChannels(oss, "input", inputChannels);
    AppendChannels(oss, "output", outputChannels);
    g_logger->Log(oss.str());
    return result;
}

bool InstallWhenLoaded()
{
    for (uint32_t attempt = 0; attempt < 450; ++attempt) {
        g_fullGame = GetModuleHandleW(kFullGameModuleName);
        if (g_fullGame)
            break;
        Sleep(100);
    }
    if (!g_fullGame) {
        g_logger->Log("fullgame.dll result trace load timed out");
        return false;
    }

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_fullGame, ".text", textStart, textSize))
        return false;
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kMergeSignature);
    if (!target) {
        g_logger->Log("FullGame result merge signature preflight failed");
        return false;
    }
    void* trampoline = JumpHook::MakeTrampoline(target, kPatchLen);
    if (!trampoline)
        return false;
    g_original = reinterpret_cast<MergeChannelsFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookMergeChannels), kPatchLen)) {
        return false;
    }
    g_logger->Log("FullGame result-channel read-only hook installed at " +
        VehicleSeatTrace::Hex(target));
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded())
        g_logger->Log("FullGame result-channel deferred install failed");
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
        logger.Log("FullGame result-channel deferred thread failed");
        return false;
    }
    CloseHandle(thread);
    logger.Log("FullGame result-channel deferred install started");
    return true;
}

} // namespace FullGameResultTrace
