#include "pch.h"
#include "RideOnMessageTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace RideOnMessageTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kEntityMessageSendRva = 0x1401618C0ull - kImageBase;
constexpr uintptr_t kRideOnUpdateMessageRetRva = 0x140F99D28ull - kImageBase;
constexpr size_t kEntityMessageSendPatchLen = 15;

using EntityMessageSendFn = void(__fastcall*)(
    uintptr_t queue, uintptr_t lock, uintptr_t message, int mode);

std::atomic<bool> g_started{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
EntityMessageSendFn g_originalSend = nullptr;

void __fastcall HookEntityMessageSend(
    uintptr_t queue, uintptr_t lock, uintptr_t message, int mode)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t expected = reinterpret_cast<uintptr_t>(g_module) + kRideOnUpdateMessageRetRva;
    if (caller == expected) {
        uintptr_t vtable = 0;
        uintptr_t payload = 0;
        uint8_t byte8 = 0;
        uint8_t byte12 = 0;
        uint16_t word10 = 0;
        VehicleSeatTrace::ReadValue(message, vtable);
        VehicleSeatTrace::ReadValue(message + 0x8, byte8);
        VehicleSeatTrace::ReadValue(message + 0x10, word10);
        VehicleSeatTrace::ReadValue(message + 0x12, byte12);
        VehicleSeatTrace::ReadValue(message + 0x18, payload);

        std::ostringstream oss;
        oss << "RideOnEntityMessage suppressed queue=" << VehicleSeatTrace::Hex(queue)
            << " lock=" << VehicleSeatTrace::Hex(lock)
            << " message=" << VehicleSeatTrace::Hex(message)
            << " mode=" << mode
            << " vtbl=" << VehicleSeatTrace::Hex(vtable)
            << " b8=" << static_cast<int>(byte8)
            << " word10=0x" << std::hex << word10 << std::dec
            << " b12=" << static_cast<int>(byte12)
            << " payload=" << VehicleSeatTrace::Hex(payload);
        g_logger->Log(oss.str());
        return;
    }
    g_originalSend(queue, lock, message, mode);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kEntityMessageSendRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kEntityMessageSendPatchLen);
    if (!trampoline) {
        logger.Log("InstallRideOnEntityMessageHook trampoline failed");
        return false;
    }
    g_originalSend = reinterpret_cast<EntityMessageSendFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookEntityMessageSend), kEntityMessageSendPatchLen)) {
        logger.Log("InstallRideOnEntityMessageHook failed");
        return false;
    }
    return true;
}

} // namespace RideOnMessageTrace
