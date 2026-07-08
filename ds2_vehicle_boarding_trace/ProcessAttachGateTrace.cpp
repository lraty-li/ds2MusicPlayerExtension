#include "pch.h"
#include "ProcessAttachGateTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>

namespace ProcessAttachGateTrace {
namespace {

constexpr const char* kProcessAttachSignature =
    "4C 8B DC 55 56 49 8D AB ? ? ? ? 48 81 EC ? ? ? ? "
    "48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B 81";
constexpr size_t kProcessAttachPatchLen = 12;

using ProcessAttachFn = void(__fastcall*)(uintptr_t rideOn);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{32};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
ProcessAttachFn g_originalProcessAttach = nullptr;

bool ShouldForceGate(const VehicleSeatTrace::Snapshot& s)
{
    return s.current == 1 && s.next == 1 && s.stage == 2 &&
        s.b18A && !s.b18B && s.b191;
}

bool TrySetOwnerGate(uintptr_t rideOn, uint32_t& before, uint32_t& after)
{
    uintptr_t owner = 0;
    if (!VehicleSeatTrace::ReadValue(rideOn + 0xA0, owner) || !owner)
        return false;
    if (!VehicleSeatTrace::ReadValue(owner + 0x7378, before))
        return false;

    after = before | (1u << 24);
    __try {
        *reinterpret_cast<uint32_t*>(owner + 0x7378) = after;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ResolveProcessAttach()
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, kProcessAttachSignature);
}

void __fastcall HookProcessAttach(uintptr_t rideOn)
{
    uintptr_t plugin = 0;
    VehicleSeatTrace::ReadValue(rideOn + 0x88, plugin);

    VehicleSeatTrace::Snapshot before = {};
    const bool haveBefore = VehicleSeatTrace::CaptureSnapshot(plugin, before);
    bool forced = false;
    uint32_t gateBefore = 0;
    uint32_t gateAfter = 0;
    if (haveBefore && ShouldForceGate(before))
        forced = TrySetOwnerGate(rideOn, gateBefore, gateAfter);

    g_originalProcessAttach(rideOn);

    VehicleSeatTrace::Snapshot after = {};
    if (!VehicleSeatTrace::CaptureSnapshot(plugin, after))
        return;

    if (forced || (haveBefore && before.b18B != after.b18B)) {
        const int remaining = g_logBudget.fetch_sub(1);
        if (remaining > 0) {
            std::ostringstream oss;
            oss << "ProcessAttach gate"
                << " forced=" << (forced ? 1 : 0)
                << " owner7378=0x" << std::hex << gateBefore
                << "->0x" << gateAfter << std::dec
                << " b18B " << static_cast<int>(before.b18B)
                << "->" << static_cast<int>(after.b18B)
                << VehicleSeatTrace::FormatSnapshot(plugin, after);
            g_logger->Log(oss.str());
        }
    }
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = ResolveProcessAttach();
    if (!target) {
        logger.Log("ProcessAttach signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "ProcessAttach resolved at " << VehicleSeatTrace::Hex(target);
    logger.Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kProcessAttachPatchLen);
    if (!trampoline) {
        logger.Log("ProcessAttach trampoline failed");
        return false;
    }

    g_originalProcessAttach = reinterpret_cast<ProcessAttachFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookProcessAttach),
            kProcessAttachPatchLen)) {
        logger.Log("ProcessAttach hook failed");
        return false;
    }

    return true;
}

} // namespace ProcessAttachGateTrace
