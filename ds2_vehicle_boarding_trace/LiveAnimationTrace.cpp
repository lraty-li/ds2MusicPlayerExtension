#include "pch.h"
#include "LiveAnimationTrace.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace LiveAnimationTrace {
namespace {

constexpr const char* kAnimEvalSignature =
    "48 8B 01 44 8B C2 89 91 ? ? ? ? 48 FF A0 ? ? ? ?";
constexpr size_t kAnimEvalPatchLen = 12;

using AnimEvalFn = int64_t(__fastcall*)(uintptr_t inner, uint32_t state);

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{80};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
AnimEvalFn g_originalAnimEval = nullptr;

uintptr_t ResolveTextSignature(const char* signature)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, signature);
}

template <typename T>
T ReadOr(uintptr_t addr, T fallback)
{
    T value = fallback;
    VehicleSeatTrace::ReadValue(addr, value);
    return value;
}

void LogInner(const char* label, uintptr_t inner, uint32_t state)
{
    const int remaining = g_logBudget.fetch_sub(1);
    if (remaining <= 0)
        return;

    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t owner = ReadOr<uintptr_t>(inner + 0x28, 0);
    const uintptr_t activeTree = ReadOr<uintptr_t>(inner + 0x3B8, 0);
    const uint32_t v20 = ReadOr<uint32_t>(inner + 0x20, 0);
    const uint32_t v2E0 = ReadOr<uint32_t>(inner + 0x2E0, 0);
    const uint8_t b2E4 = ReadOr<uint8_t>(inner + 0x2E4, 0);
    const uint32_t v2E8 = ReadOr<uint32_t>(inner + 0x2E8, 0);
    const uint32_t v2EC = ReadOr<uint32_t>(inner + 0x2EC, 0);
    const uint32_t v398 = ReadOr<uint32_t>(inner + 0x398, 0);
    const uint32_t v3A0 = ReadOr<uint32_t>(inner + 0x3A0, 0);
    const uint8_t b3D6 = ReadOr<uint8_t>(inner + 0x3D6, 0);
    const float f544 = ReadOr<float>(inner + 0x544, 0.0f);
    const uintptr_t flags760 = ReadOr<uintptr_t>(inner + 0x760, 0);

    std::ostringstream oss;
    oss << "AnimEval " << label
        << " caller=" << VehicleSeatTrace::Hex(caller)
        << " inner=" << VehicleSeatTrace::Hex(inner)
        << " state=" << state
        << " owner=" << VehicleSeatTrace::Hex(owner)
        << " tree=" << VehicleSeatTrace::Hex(activeTree)
        << " v20=" << v20
        << " v2E0=" << v2E0
        << " b2E4=" << static_cast<int>(b2E4)
        << " v2E8=" << v2E8
        << " v2EC=" << v2EC
        << " v398=" << v398
        << " v3A0=" << v3A0
        << " b3D6=" << static_cast<int>(b3D6)
        << " f544=" << f544
        << " flags760=" << VehicleSeatTrace::Hex(flags760);
    g_logger->Log(oss.str());
}

int64_t __fastcall HookAnimEval(uintptr_t inner, uint32_t state)
{
    const bool interesting = state <= 32;
    if (interesting)
        LogInner("entry", inner, state);
    const int64_t result = g_originalAnimEval(inner, state);
    if (interesting)
        LogInner("exit", inner, state);
    return result;
}

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;

    const uintptr_t target = ResolveTextSignature(kAnimEvalSignature);
    if (!target) {
        logger.Log("AnimStateDispatch signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "AnimStateDispatch resolved at " << VehicleSeatTrace::Hex(target);
    logger.Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kAnimEvalPatchLen);
    if (!trampoline)
        return false;
    g_originalAnimEval = reinterpret_cast<AnimEvalFn>(trampoline);
    return JumpHook::WriteEntryJump(
        target, reinterpret_cast<void*>(&HookAnimEval), kAnimEvalPatchLen);
}

} // namespace LiveAnimationTrace
