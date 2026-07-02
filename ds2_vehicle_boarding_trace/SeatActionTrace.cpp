#include "pch.h"
#include "SeatActionTrace.h"
#include "JumpHook.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace SeatActionTrace {
namespace {

constexpr uintptr_t kImageBase = 0x140000000ull;
constexpr uintptr_t kActionPushRva = 0x140B198E0ull - kImageBase;
constexpr uintptr_t kDriveActionPushRetRva = 0x140F8EEF0ull - kImageBase;
constexpr size_t kActionPushPatchLen = 15;

using ActionPushFn = int64_t(__fastcall*)(uintptr_t list, const uint8_t* value);

std::atomic<bool> g_started{false};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
ActionPushFn g_originalActionPush = nullptr;

int64_t __fastcall HookActionPush(uintptr_t list, const uint8_t* value)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t expected = reinterpret_cast<uintptr_t>(g_module) + kDriveActionPushRetRva;
    if (caller == expected) {
        uint8_t action = 0;
        const bool hasAction = VehicleSeatTrace::ReadValue(
            reinterpret_cast<uintptr_t>(value), action);
        std::ostringstream oss;
        oss << "DriveSeatActionPush action=0x" << std::hex
            << static_cast<int>(action) << std::dec
            << " list=" << VehicleSeatTrace::Hex(list);
        g_logger->Log(oss.str());
    }
    return g_originalActionPush(list, value);
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = reinterpret_cast<uintptr_t>(g_module) + kActionPushRva;
    void* trampoline = JumpHook::MakeTrampoline(target, kActionPushPatchLen);
    if (!trampoline) {
        logger.Log("InstallActionPushHook trampoline failed");
        return false;
    }
    g_originalActionPush = reinterpret_cast<ActionPushFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookActionPush), kActionPushPatchLen)) {
        logger.Log("InstallActionPushHook failed");
        return false;
    }
    return true;
}

} // namespace SeatActionTrace
