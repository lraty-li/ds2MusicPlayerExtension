#include "pch.h"
#include "SeatTransitionTrace.h"
#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace SeatTransitionTrace {
namespace {

constexpr const char* kSeatTransitionSignature =
    "48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 56 48 83 EC ? "
    "48 8D 99 ? ? ? ? 48 8B F9";
constexpr const char* kProcessAttachSeatTransitionReturnSignature =
    "48 8D 4C 24 ? 0F B6 D8 E8 ? ? ? ? 84 DB 75 ? 44 38 A7 ? ? ? ? "
    "0F 85 ? ? ? ? 48 8B 8F ? ? ? ? E8";
constexpr size_t kSeatTransitionPatchLen = 18;

using SeatTransitionFn = uint8_t(__fastcall*)(
    uintptr_t seatController,
    uintptr_t* targetKey,
    uint8_t start,
    uintptr_t* callback,
    uint8_t finishFlag);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{80};
std::atomic<uintptr_t> g_activeSeatController{0};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
SeatTransitionFn g_originalSeatTransition = nullptr;
uintptr_t g_processAttachSeatTransitionReturn = 0;

uintptr_t ResolveSeatTransition()
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::FindUnique(textStart, textSize, kSeatTransitionSignature);
}

uintptr_t ResolveProcessAttachSeatTransitionReturn()
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::FindUnique(
        textStart, textSize, kProcessAttachSeatTransitionReturnSignature);
}

uintptr_t ToGameRva(uintptr_t address)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_module);
    return address >= base ? address - base : address;
}

void LogCallbackVtable(uintptr_t* callback)
{
    if (!callback)
        return;

    uintptr_t vtable = 0;
    if (!VehicleSeatTrace::ReadValue(
            reinterpret_cast<uintptr_t>(callback), vtable) || !vtable)
        return;

    uintptr_t methods[4] = {};
    for (size_t i = 0; i < _countof(methods); ++i) {
        VehicleSeatTrace::ReadValue(
            vtable + i * sizeof(uintptr_t), methods[i]);
    }

    std::ostringstream oss;
    oss << "SeatTransition callback vtableRva="
        << VehicleSeatTrace::Hex(ToGameRva(vtable));
    for (size_t i = 0; i < _countof(methods); ++i) {
        oss << " method" << i << "Rva="
            << VehicleSeatTrace::Hex(ToGameRva(methods[i]));
    }
    g_logger->Log(oss.str());
}

void LogControllerVtable(uintptr_t seatController)
{
    uintptr_t vtable = 0;
    if (!seatController ||
        !VehicleSeatTrace::ReadValue(seatController, vtable) || !vtable)
        return;

    uintptr_t startMethod = 0;
    uintptr_t finishMethod = 0;
    uintptr_t transitionDriver = 0;
    uintptr_t driverVtable = 0;
    uintptr_t driverUpdate = 0;
    uintptr_t seatAction = 0;
    VehicleSeatTrace::ReadValue(vtable + 328, startMethod);
    VehicleSeatTrace::ReadValue(vtable + 336, finishMethod);
    VehicleSeatTrace::ReadValue(seatController + 0x5D8, transitionDriver);
    if (transitionDriver &&
        VehicleSeatTrace::ReadValue(transitionDriver, driverVtable)) {
        VehicleSeatTrace::ReadValue(driverVtable + 16, driverUpdate);
    }
    VehicleSeatTrace::ReadValue(seatController + 0x340, seatAction);

    std::ostringstream oss;
    oss << "SeatTransition controller vtableRva="
        << VehicleSeatTrace::Hex(ToGameRva(vtable))
        << " startMethodRva=" << VehicleSeatTrace::Hex(ToGameRva(startMethod))
        << " finishMethodRva=" << VehicleSeatTrace::Hex(ToGameRva(finishMethod))
        << " driver=" << VehicleSeatTrace::Hex(transitionDriver)
        << " driverVtableRva=" << VehicleSeatTrace::Hex(ToGameRva(driverVtable))
        << " driverUpdateRva=" << VehicleSeatTrace::Hex(ToGameRva(driverUpdate))
        << " seatAction=" << VehicleSeatTrace::Hex(seatAction);
    g_logger->Log(oss.str());
}

uint8_t __fastcall HookSeatTransition(
    uintptr_t seatController,
    uintptr_t* targetKey,
    uint8_t start,
    uintptr_t* callback,
    uint8_t finishFlag)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool isProcessAttachStart =
        caller == g_processAttachSeatTransitionReturn && start != 0;
    if (isProcessAttachStart)
        g_activeSeatController.store(seatController);
    else if (!start && g_activeSeatController.load() == seatController)
        g_activeSeatController.store(0);
    uintptr_t targetKeyValue = 0;
    if (targetKey)
        VehicleSeatTrace::ReadValue(reinterpret_cast<uintptr_t>(targetKey), targetKeyValue);

    const int remaining = g_logBudget.fetch_sub(1);
    if (remaining > 0 || isProcessAttachStart) {
        std::ostringstream oss;
        oss << "SeatTransition call"
            << " caller=" << VehicleSeatTrace::Hex(caller)
            << " controller=" << VehicleSeatTrace::Hex(seatController)
            << " targetKeyPtr=" << VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(targetKey))
            << " targetKey=" << VehicleSeatTrace::Hex(targetKeyValue)
            << " start=" << static_cast<int>(start)
            << " finishFlag=" << static_cast<int>(finishFlag)
            << " callback=" << VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(callback));
        g_logger->Log(oss.str());
    }
    if (isProcessAttachStart) {
        LogCallbackVtable(callback);
        LogControllerVtable(seatController);
    }

    const uint8_t result = g_originalSeatTransition(
        seatController, targetKey, start, callback, finishFlag);
    if (isProcessAttachStart) {
        std::ostringstream oss;
        oss << "SeatTransition ProcessAttach original result="
            << static_cast<int>(result);
        g_logger->Log(oss.str());
    }
    return result;
}

} // namespace

uintptr_t ActiveSeatController()
{
    return g_activeSeatController.load();
}

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    g_processAttachSeatTransitionReturn = ResolveProcessAttachSeatTransitionReturn();
    if (!g_processAttachSeatTransitionReturn) {
        logger.Log("ProcessAttach SeatTransition return signature not found");
        return false;
    }

    const uintptr_t target = ResolveSeatTransition();
    if (!target) {
        logger.Log("InstallSeatTransitionHook signature not found");
        return false;
    }
    {
        std::ostringstream oss;
        oss << "SeatTransition resolved at " << VehicleSeatTrace::Hex(target);
        logger.Log(oss.str());
    }

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
