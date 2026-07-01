#include "pch.h"
#include "JumpHook.h"

#include <cstring>

namespace JumpHook {
namespace {

bool IsRel32Reachable(uintptr_t from, uintptr_t to)
{
    const intptr_t delta = static_cast<intptr_t>(to - (from + 5));
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

void WriteRel32JumpBytes(uint8_t* dst, uintptr_t from, uintptr_t to)
{
    dst[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(to - (from + 5));
    memcpy(dst + 1, &rel, sizeof(rel));
}

void* AllocateRelayNear(uintptr_t target, uintptr_t destination)
{
    constexpr uintptr_t kStep = 0x10000;
    constexpr uintptr_t kMaxDistance = 0x7FFF0000;

    for (uintptr_t delta = kStep; delta < kMaxDistance; delta += kStep) {
        uintptr_t hints[2] = { target + delta, target - delta };
        for (uintptr_t hint : hints) {
            auto* relay = reinterpret_cast<uint8_t*>(VirtualAlloc(
                reinterpret_cast<void*>(hint),
                16,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE));
            if (!relay)
                continue;
            if (!IsRel32Reachable(target, reinterpret_cast<uintptr_t>(relay))) {
                VirtualFree(relay, 0, MEM_RELEASE);
                continue;
            }
            WriteAbsoluteJumpBytes(relay, destination);
            FlushInstructionCache(GetCurrentProcess(), relay, 16);
            return relay;
        }
    }
    return nullptr;
}

} // namespace

void WritePreservingAbsoluteJumpBytes(uint8_t* dst, uintptr_t to)
{
    dst[0] = 0x50;
    dst[1] = 0x48;
    dst[2] = 0xB8;
    memcpy(dst + 3, &to, sizeof(to));
    dst[11] = 0x48;
    dst[12] = 0x87;
    dst[13] = 0x04;
    dst[14] = 0x24;
    dst[15] = 0xC3;
}

void WriteAbsoluteJumpBytes(uint8_t* dst, uintptr_t to)
{
    dst[0] = 0x48;
    dst[1] = 0xB8;
    memcpy(dst + 2, &to, sizeof(to));
    dst[10] = 0xFF;
    dst[11] = 0xE0;
}

void* MakeTrampoline(uintptr_t target, size_t patchLen)
{
    auto* trampoline = reinterpret_cast<uint8_t*>(VirtualAlloc(
        nullptr, patchLen + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline)
        return nullptr;

    memcpy(trampoline, reinterpret_cast<void*>(target), patchLen);
    WritePreservingAbsoluteJumpBytes(trampoline + patchLen, target + patchLen);
    FlushInstructionCache(GetCurrentProcess(), trampoline, patchLen + 16);
    return trampoline;
}

bool WriteEntryJump(uintptr_t target, void* hook, size_t patchLen)
{
    uintptr_t destination = reinterpret_cast<uintptr_t>(hook);
    if (!IsRel32Reachable(target, destination)) {
        void* relay = AllocateRelayNear(target, destination);
        if (!relay)
            return false;
        destination = reinterpret_cast<uintptr_t>(relay);
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), patchLen,
            PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    auto* dst = reinterpret_cast<uint8_t*>(target);
    WriteRel32JumpBytes(dst, target, destination);
    for (size_t i = 5; i < patchLen; ++i)
        dst[i] = 0x90;
    FlushInstructionCache(GetCurrentProcess(), dst, patchLen);

    DWORD ignored = 0;
    VirtualProtect(dst, patchLen, oldProtect, &ignored);
    return true;
}

} // namespace JumpHook
