#include "pch.h"
#include "FullGameAnimationTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <sstream>

namespace FullGameAnimationTrace {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kPlayspeedNodeSignature =
    "41 57 41 56 41 55 41 54 56 57 55 53 48 81 EC ? ? ? ? "
    "0F 29 B4 24 ? ? ? ? 48 8B B4 24";
constexpr size_t kPatchLen = 12;
constexpr const char* kSyncTimeSignature = "56 57 4C 8B 42";
constexpr size_t kSyncTimePatchLen = 12;
constexpr const char* kBoardingSyncSourceSignature =
    "48 63 C7 48 8B 8C C4 80 00 00 00 4C 89 FA 41 0F 28 D2 "
    "E8 ? ? ? ? 41 80 BD 7D 13 00 00 00";
constexpr const char* kBoardingSyncLayer2Signature =
    "48 8B 8C C4 80 00 00 00 48 8B 94 24 D8 01 00 00 "
    "41 0F 28 D2 E8 ? ? ? ? 41 8A 85 81 13 00 00";
constexpr const char* kBoardingSyncLayer3Signature =
    "48 63 C7 48 8B 8C C4 80 00 00 00 48 8B 94 24 C8 01 00 00 "
    "41 0F 28 D2 E8 ? ? ? ? 41 80 BD 6A 13 00 00 00";
constexpr const char* kBoardingSyncLayer4Signature =
    "48 63 C7 48 8B 8C C4 80 00 00 00 48 8B 94 24 B8 01 00 00 "
    "41 0F 28 D2 E8 ? ? ? ? 41 80 BD 73 13 00 00 00";
constexpr float kFastBoardingSyncScale = 512.0f;

using PlayspeedNodeFn = void(__fastcall*)(
    float, uint8_t, float, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using SyncTimeFn = uint8_t(__fastcall*)(uintptr_t, uintptr_t, float);

std::atomic<bool> g_started{false};
std::atomic<uint32_t> g_lastBucket{UINT32_MAX};
std::array<std::atomic<uintptr_t>, 64> g_syncCallers{};
HMODULE g_fullGame = nullptr;
const Logger* g_logger = nullptr;
PlayspeedNodeFn g_original = nullptr;
SyncTimeFn g_originalSyncTime = nullptr;
std::array<uintptr_t, 4> g_boardingSyncReturns{};
std::atomic<uint32_t> g_fastSyncFrame{UINT32_MAX};

bool IsBoardingSyncCaller(uintptr_t caller)
{
    for (const uintptr_t candidate : g_boardingSyncReturns) {
        if (candidate == caller)
            return true;
    }
    return false;
}

bool RememberSyncCaller(uintptr_t caller)
{
    for (auto& slot : g_syncCallers) {
        uintptr_t value = slot.load();
        if (value == caller)
            return false;
        if (!value && slot.compare_exchange_strong(value, caller))
            return true;
    }
    return false;
}

uint8_t __fastcall HookSyncTime(uintptr_t output, uintptr_t input, float scale)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool suppressionActive =
        RideOnEnterInterceptor::FastBoardingSuppressionActive();
    if (!suppressionActive)
        g_fastSyncFrame.store(UINT32_MAX);
    uintptr_t inputState = 0;
    uint32_t inputFrame = UINT32_MAX;
    VehicleSeatTrace::ReadValue(input + 0x8, inputState);
    if (inputState)
        VehicleSeatTrace::ReadValue(inputState + 0x8, inputFrame);
    uint32_t expectedFrame = UINT32_MAX;
    if (suppressionActive && IsBoardingSyncCaller(caller) &&
        inputFrame != UINT32_MAX) {
        g_fastSyncFrame.compare_exchange_strong(expectedFrame, inputFrame);
    }
    const bool fastBoardingSync = suppressionActive &&
        IsBoardingSyncCaller(caller) &&
        g_fastSyncFrame.load() == inputFrame;
    const float effectiveScale = fastBoardingSync ? kFastBoardingSyncScale : scale;
    const uint8_t result = g_originalSyncTime(output, input, effectiveScale);
    const uintptr_t rideOn = RideOnEnterInterceptor::ActiveBoardingRideOn();
    if (!rideOn || !RememberSyncCaller(caller))
        return result;

    float elapsed = 0.0f;
    uintptr_t outputState = 0;
    uint32_t outputFrame = 0;
    uint8_t inputInstant = 0;
    uint8_t outputInstant = 0;
    double inputStart = 0.0;
    double inputEnd = 0.0;
    double outputStart = 0.0;
    double outputEnd = 0.0;
    float inputOverride = 0.0f;
    float outputOverride = 0.0f;
    float inputDuration = 0.0f;
    float inputSyncDuration = 0.0f;
    float outputDuration = 0.0f;
    float outputSyncDuration = 0.0f;
    VehicleSeatTrace::ReadValue(rideOn + 0x180, elapsed);
    VehicleSeatTrace::ReadValue(output + 0x8, outputState);
    VehicleSeatTrace::ReadValue(input + 0x50, inputOverride);
    VehicleSeatTrace::ReadValue(output + 0x50, outputOverride);
    VehicleSeatTrace::ReadValue(input + 0x48, inputDuration);
    VehicleSeatTrace::ReadValue(input + 0x4C, inputSyncDuration);
    VehicleSeatTrace::ReadValue(output + 0x48, outputDuration);
    VehicleSeatTrace::ReadValue(output + 0x4C, outputSyncDuration);
    if (inputState) {
        VehicleSeatTrace::ReadValue(inputState + 0xC, inputInstant);
        VehicleSeatTrace::ReadValue(inputState + 0x10, inputStart);
        VehicleSeatTrace::ReadValue(inputState + 0x18, inputEnd);
    }
    if (outputState) {
        VehicleSeatTrace::ReadValue(outputState + 0x8, outputFrame);
        VehicleSeatTrace::ReadValue(outputState + 0xC, outputInstant);
        VehicleSeatTrace::ReadValue(outputState + 0x10, outputStart);
        VehicleSeatTrace::ReadValue(outputState + 0x18, outputEnd);
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(g_fullGame);
    std::ostringstream oss;
    oss << "FullGameSyncTime callerRva=" << VehicleSeatTrace::Hex(caller - base)
        << " elapsed=" << elapsed
        << " scale=" << scale << "->" << effectiveScale
        << " result=" << static_cast<int>(result)
        << " input=" << VehicleSeatTrace::Hex(input)
        << " inputState=" << VehicleSeatTrace::Hex(inputState)
        << " inputFrame=" << inputFrame
        << " inputInstant=" << static_cast<int>(inputInstant)
        << " inputRange=" << inputStart << ".." << inputEnd
        << " inputOverride=" << inputOverride
        << " inputDuration=" << inputDuration
        << " inputSyncDuration=" << inputSyncDuration
        << " output=" << VehicleSeatTrace::Hex(output)
        << " outputState=" << VehicleSeatTrace::Hex(outputState)
        << " outputFrame=" << outputFrame
        << " outputInstant=" << static_cast<int>(outputInstant)
        << " outputRange=" << outputStart << ".." << outputEnd
        << " outputOverride=" << outputOverride
        << " outputDuration=" << outputDuration
        << " outputSyncDuration=" << outputSyncDuration;
    g_logger->Log(oss.str());
    return result;
}

void __fastcall HookPlayspeedNode(
    float timeScale, uint8_t applyTimeScale, float multiplier, uintptr_t output,
    uintptr_t a5, uintptr_t a6, uintptr_t a7, uintptr_t a8,
    uintptr_t a9, uintptr_t a10, uintptr_t a11, uintptr_t a12,
    uintptr_t a13, uintptr_t a14, uintptr_t a15, uintptr_t a16,
    uintptr_t a17, uintptr_t a18, uintptr_t a19, uintptr_t a20,
    uintptr_t a21, uintptr_t a22, uintptr_t a23, uintptr_t a24)
{
    g_original(
        timeScale, applyTimeScale, multiplier, output,
        a5, a6, a7, a8, a9, a10, a11, a12,
        a13, a14, a15, a16, a17, a18, a19, a20,
        a21, a22, a23, a24);

    const uintptr_t rideOn = RideOnEnterInterceptor::ActiveBoardingRideOn();
    float elapsed = 0.0f;
    if (!rideOn || !VehicleSeatTrace::ReadValue(rideOn + 0x180, elapsed))
        return;
    const uint32_t bucket = static_cast<uint32_t>(elapsed * 4.0f);
    if (g_lastBucket.exchange(bucket) == bucket)
        return;

    float duration = 0.0f;
    float syncDuration = 0.0f;
    uint8_t resultFlags = 0;
    VehicleSeatTrace::ReadValue(output + 0x48, duration);
    VehicleSeatTrace::ReadValue(output + 0x4C, syncDuration);
    VehicleSeatTrace::ReadValue(output + 0x58, resultFlags);

    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_fullGame);
    std::ostringstream oss;
    oss << "FullGamePlayspeed elapsed=" << elapsed
        << " callerRva=" << VehicleSeatTrace::Hex(caller - base)
        << " timeScale=" << timeScale
        << " apply=" << static_cast<int>(applyTimeScale)
        << " multiplier=" << multiplier
        << " output=" << VehicleSeatTrace::Hex(output)
        << " childState=" << VehicleSeatTrace::Hex(a5)
        << " duration=" << duration
        << " syncDuration=" << syncDuration
        << " resultFlags=0x" << std::hex << static_cast<int>(resultFlags);
    g_logger->Log(oss.str());
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
        g_logger->Log("fullgame.dll deferred load timed out");
        return false;
    }

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_fullGame, ".text", textStart, textSize))
        return false;
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kPlayspeedNodeSignature);
    const uintptr_t syncTarget = PatternScan::FindUnique(
        textStart, textSize, kSyncTimeSignature);
    const uintptr_t boardingSyncCall = PatternScan::FindUnique(
        textStart, textSize, kBoardingSyncSourceSignature);
    const uintptr_t boardingSyncLayer2 = PatternScan::FindUnique(
        textStart, textSize, kBoardingSyncLayer2Signature);
    const uintptr_t boardingSyncLayer3 = PatternScan::FindUnique(
        textStart, textSize, kBoardingSyncLayer3Signature);
    const uintptr_t boardingSyncLayer4 = PatternScan::FindUnique(
        textStart, textSize, kBoardingSyncLayer4Signature);
    if (!target || !syncTarget || !boardingSyncCall || !boardingSyncLayer2 ||
        !boardingSyncLayer3 || !boardingSyncLayer4) {
        g_logger->Log("FullGame animation signature preflight failed");
        return false;
    }
    g_boardingSyncReturns = {
        boardingSyncCall + 0x17, boardingSyncLayer2 + 0x19,
        boardingSyncLayer3 + 0x1C, boardingSyncLayer4 + 0x1C};

    void* trampoline = JumpHook::MakeTrampoline(target, kPatchLen);
    void* syncTrampoline = JumpHook::MakeTrampoline(syncTarget, kSyncTimePatchLen);
    if (!trampoline || !syncTrampoline)
        return false;
    g_original = reinterpret_cast<PlayspeedNodeFn>(trampoline);
    g_originalSyncTime = reinterpret_cast<SyncTimeFn>(syncTrampoline);
    if (!JumpHook::WriteEntryJump(
            syncTarget, reinterpret_cast<void*>(&HookSyncTime), kSyncTimePatchLen) ||
        !JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookPlayspeedNode), kPatchLen)) {
        return false;
    }
    g_logger->Log("FullGame animation read-only hooks installed playspeed=" +
        VehicleSeatTrace::Hex(target) +
        " syncTime=" + VehicleSeatTrace::Hex(syncTarget));
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded())
        g_logger->Log("FullGame animation deferred install failed");
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
        logger.Log("FullGame animation deferred thread failed");
        return false;
    }
    CloseHandle(thread);
    logger.Log("FullGame animation deferred install started");
    return true;
}

} // namespace FullGameAnimationTrace
