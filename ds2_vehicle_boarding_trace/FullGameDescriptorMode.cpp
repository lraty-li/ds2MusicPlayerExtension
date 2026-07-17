#include "pch.h"
#include "FullGameDescriptorMode.h"

#include "PatternScan.h"
#include "RideOnEnterInterceptor.h"
#include "VehicleSnapshot.h"

#include <atomic>
#include <cstdint>
#include <sstream>

namespace FullGameDescriptorMode {
namespace {

constexpr wchar_t kFullGameModuleName[] = L"fullgame.dll";
constexpr const char* kBoardingDescriptorCallSignature =
    "3D 58 A7 C4 0B 0F 85 ? ? ? ? 41 80 BE BB 07 05 00 00 "
    "0F 84 ? ? ? ? 48 8B 95 30 27 00 00 C6 44 24 20 01 "
    "48 8B 8C 24 ? ? ? ? 45 31 C0 41 0F 28 D9 FF 15 ? ? ? ?";
constexpr uint32_t kIndirectCallOffset = 0x34;
constexpr uint32_t kCallInstructionSize = 6;

using EvaluateDescriptorFn = void(__fastcall*)(
    uintptr_t output, uintptr_t descriptor, uint8_t mode, float weight);

std::atomic<bool> g_started{false};
std::atomic<bool> g_loggedCall{false};
HMODULE g_fullGame = nullptr;
const Logger* g_logger = nullptr;
uintptr_t g_boardingReturn = 0;
EvaluateDescriptorFn g_original = nullptr;

bool IsExecutableAddress(uintptr_t address)
{
    MEMORY_BASIC_INFORMATION memory = {};
    if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)))
        return false;
    const DWORD protect = memory.Protect & 0xFF;
    return memory.State == MEM_COMMIT &&
        (protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ ||
         protect == PAGE_EXECUTE_READWRITE ||
         protect == PAGE_EXECUTE_WRITECOPY);
}

void __fastcall HookEvaluateDescriptor(
    uintptr_t output, uintptr_t descriptor, uint8_t mode, float weight)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool activeBoardingCall = caller == g_boardingReturn &&
        RideOnEnterInterceptor::FastBoardingSuppressionActive();
    const float effectiveWeight = weight;
    g_original(output, descriptor, mode, effectiveWeight);

    if (!activeBoardingCall || g_loggedCall.exchange(true))
        return;
    float duration = 0.0f;
    float syncDuration = 0.0f;
    uintptr_t single = 0;
    VehicleSeatTrace::ReadValue(output + 0x40, single);
    VehicleSeatTrace::ReadValue(output + 0x48, duration);
    VehicleSeatTrace::ReadValue(output + 0x4C, syncDuration);
    std::ostringstream oss;
    oss << "FastBoarding descriptor weight suppression"
        << " mode=" << static_cast<uint32_t>(mode)
        << " weight=" << weight << "->" << effectiveWeight
        << " descriptor=" << VehicleSeatTrace::Hex(descriptor)
        << " single=" << VehicleSeatTrace::Hex(single)
        << " duration=" << duration
        << " syncDuration=" << syncDuration;
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
    if (!g_fullGame)
        return false;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(g_fullGame, ".text", textStart, textSize))
        return false;
    const uintptr_t callBranch = PatternScan::FindUnique(
        textStart, textSize, kBoardingDescriptorCallSignature);
    if (!callBranch) {
        g_logger->Log("Boarding descriptor call signature not unique");
        return false;
    }

    const uintptr_t callInstruction = callBranch + kIndirectCallOffset;
    const uintptr_t slotAddress = PatternScan::ResolveRip(callInstruction, 2);
    auto* slot = reinterpret_cast<void* volatile*>(slotAddress);
    void* original = *slot;
    if (!original || !IsExecutableAddress(reinterpret_cast<uintptr_t>(original))) {
        g_logger->Log("Boarding descriptor evaluator slot is not ready");
        return false;
    }

    g_boardingReturn = callInstruction + kCallInstructionSize;
    g_original = reinterpret_cast<EvaluateDescriptorFn>(original);
    void* replaced = InterlockedExchangePointer(
        slot, reinterpret_cast<void*>(&HookEvaluateDescriptor));
    if (replaced != original) {
        InterlockedExchangePointer(slot, replaced);
        g_logger->Log("Boarding descriptor evaluator slot changed during install");
        return false;
    }
    g_logger->Log("Boarding descriptor mode hook installed slot=" +
        VehicleSeatTrace::Hex(slotAddress) +
        " target=" + VehicleSeatTrace::Hex(reinterpret_cast<uintptr_t>(original)));
    return true;
}

DWORD WINAPI DeferredInstallThread(LPVOID)
{
    if (!InstallWhenLoaded())
        g_logger->Log("Boarding descriptor mode deferred install failed");
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
    if (!thread)
        return false;
    CloseHandle(thread);
    logger.Log("Boarding descriptor mode deferred install started");
    return true;
}

} // namespace FullGameDescriptorMode
