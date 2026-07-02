#include "pch.h"
#include "SeatTransitionTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace SeatTransitionTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kSeatTransitionRva = 0x141F6BDC0ull - kImageBase;
constexpr uintptr_t kProcessAttachSeatTransitionRetRva = 0x140F9AD36ull - kImageBase;
constexpr size_t kSeatTransitionPatchLen = 18;
constexpr bool kSuppressProcessAttachStart = false;

using SeatTransitionFn = uint8_t(__fastcall*)(
    uintptr_t seatController,
    uintptr_t* targetKey,
    uint8_t start,
    uintptr_t* callback,
    uint8_t finishFlag);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{80};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
SeatTransitionFn g_originalSeatTransition = nullptr;

uint8_t __fastcall HookSeatTransition(
    uintptr_t seatController,
    uintptr_t* targetKey,
    uint8_t start,
    uintptr_t* callback,
    uint8_t finishFlag)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t processAttachCaller =
        reinterpret_cast<uintptr_t>(g_module) + kProcessAttachSeatTransitionRetRva;
    const bool isProcessAttachStart = caller == processAttachCaller && start != 0;

    const int remaining = g_logBudget.fetch_sub(1);
    if (remaining > 0 || isProcessAttachStart) {
        std::ostringstream oss;
        oss << "SeatTransition call"
            << " caller=" << VehicleSeatTrace::Hex(caller)
            << " controller=" << VehicleSeatTrace::Hex(seatController)
            << " targetKeyPtr=" << VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(targetKey))
            << " targetKey=" << VehicleSeatTrace::Hex(targetKey ? *targetKey : 0)
            << " start=" << static_cast<int>(start)
            << " finishFlag=" << static_cast<int>(finishFlag)
            << " callback=" << VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(callback));
        if (isProcessAttachStart && kSuppressProcessAttachStart)
            oss << " suppressed-return=1";
        g_logger->Log(oss.str());
    }

    if (isProcessAttachStart && kSuppressProcessAttachStart)
        return 1;

    return g_originalSeatTransition(
        seatController, targetKey, start, callback, finishFlag);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kSeatTransitionRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kSeatTransitionPatchLen);
    if (!trampoline) {
        logger.Log("InstallSeatTransitionHook trampoline failed");
        return false;
    }
    g_originalSeatTransition = reinterpret_cast<SeatTransitionFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookSeatTransition),
            kSeatTransitionPatchLen)) {
        logger.Log("InstallSeatTransitionHook failed");
        return false;
    }
    return true;
}

} // namespace SeatTransitionTrace
