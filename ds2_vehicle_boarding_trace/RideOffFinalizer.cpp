#include "pch.h"
#include "RideOffFinalizer.h"

#include "PatternScan.h"

#include <atomic>
#include <cstdint>

namespace RideOffFinalizer {
namespace {

constexpr const char* kFinalizeSignature =
    "48 89 6C 24 ? 57 48 83 EC 20 48 8B 41 ? 0F B6 EA";

using FinalizeFn = int64_t(__fastcall*)(
    uintptr_t runtime, uint8_t specialMode, uint8_t force);

std::atomic<bool> g_started{false};
FinalizeFn g_finalize = nullptr;

} // namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.load(std::memory_order_acquire))
        return true;
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
        return false;
    const uintptr_t target = PatternScan::FindUnique(
        textStart, textSize, kFinalizeSignature);
    if (!target)
        return false;
    g_finalize = reinterpret_cast<FinalizeFn>(target);
    g_started.store(true, std::memory_order_release);
    logger.Log("RideOff native finalizer resolved");
    return true;
}

void Force(uintptr_t runtime, uint8_t specialMode)
{
    if (runtime && g_finalize)
        g_finalize(runtime, specialMode, 1);
}

} // namespace RideOffFinalizer
