#include "pch.h"
#include "PresentationSuppressor.h"

#include "JumpHook.h"
#include "PatternScan.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace PresentationSuppressor {
namespace {

// Match PresentationGlobal_RequestAction
// Prologue: mov [rsp+arg_0],rbx; mov [rsp+arg_8],rbp; mov [rsp+arg_10],rsi; push rdi; ...
constexpr const char* kPresentationRequestSignature =
    "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? "
    "48 8D B9 ? ? ? ? 48 8B D9 48 8B CF 41 8B E9";
constexpr size_t kPresentationRequestPatchLen = 20;

using PresentationRequestFn = void(__fastcall*)(
    uintptr_t global, uint32_t actionHash, uintptr_t a3,
    int32_t a4, uintptr_t target, uint8_t force);

constexpr uint32_t kBoardingPresentationHashes[] = {
    0x53758BEDu,
    0x6F53F3A5u,
    0x3897A3D5u,
};

std::atomic<bool> g_started{false};
std::atomic<int> g_logBudget{8};
HMODULE g_module = nullptr;
const Logger* g_logger = nullptr;
PresentationRequestFn g_originalRequest = nullptr;

bool IsBoardingPresentationHash(uint32_t actionHash)
{
    for (const uint32_t hash : kBoardingPresentationHashes) {
        if (actionHash == hash)
            return true;
    }
    return false;
}

uintptr_t ResolvePresentationRequest()
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_module, ".text", textStart, textSize))
        return 0;
    return PatternScan::Find(textStart, textSize, kPresentationRequestSignature);
}

void __fastcall HookPresentationRequest(
    uintptr_t global, uint32_t actionHash, uintptr_t a3,
    int32_t a4, uintptr_t target, uint8_t force)
{
    // Suppress the three known player boarding presentation requests.
    if (IsBoardingPresentationHash(actionHash)) {
        if (g_logBudget.fetch_sub(1) > 0) {
            std::ostringstream oss;
            oss << "Presentation suppressed hash=0x" << std::hex << actionHash
                << " target=" << VehicleSeatTrace::Hex(target)
                << " force=" << static_cast<int>(force);
            g_logger->Log(oss.str());
        }
        return; // Suppressed: don't call original
    }

    g_originalRequest(global, actionHash, a3, a4, target, force);
}

} // anonymous namespace

bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    if (g_started.exchange(true))
        return true;

    g_module = gameModule;
    g_logger = &logger;
    logger.Log("PresentationSuppressor v0.2.0: suppress all known player boarding presentations");

    const uintptr_t target = ResolvePresentationRequest();
    if (!target) {
        logger.Log("PresentationRequest signature not found");
        return false;
    }

    std::ostringstream oss;
    oss << "PresentationRequest resolved at " << VehicleSeatTrace::Hex(target);
    logger.Log(oss.str());

    void* trampoline = JumpHook::MakeTrampoline(target, kPresentationRequestPatchLen);
    if (!trampoline) {
        logger.Log("PresentationRequest trampoline failed");
        return false;
    }

    g_originalRequest = reinterpret_cast<PresentationRequestFn>(trampoline);
    if (!JumpHook::WriteEntryJump(
            target, reinterpret_cast<void*>(&HookPresentationRequest),
            kPresentationRequestPatchLen)) {
        logger.Log("PresentationRequest hook failed");
        return false;
    }

    logger.Log("PresentationSuppressor installed");
    return true;
}

} // namespace PresentationSuppressor
