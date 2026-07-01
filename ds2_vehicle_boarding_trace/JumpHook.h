#pragma once

#include <windows.h>
#include <cstdint>

namespace JumpHook {
    void WriteAbsoluteJumpBytes(uint8_t* dst, uintptr_t to);
    void* MakeTrampoline(uintptr_t target, size_t patchLen);
    bool WriteEntryJump(uintptr_t target, void* hook, size_t patchLen);
}
