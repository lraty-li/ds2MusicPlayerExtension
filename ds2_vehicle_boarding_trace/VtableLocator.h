#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace VtableLocator {

struct Match {
    uintptr_t target = 0;
    uintptr_t vtable = 0;
    uintptr_t slot = 0;
    uintptr_t col = 0;
    uint32_t slotIndex = 0;
    uint32_t subobjectOffset = 0;
    size_t pointerMatches = 0;
    size_t rttiMatches = 0;
    std::string typeName;
};

bool FindUnique(HMODULE module, const char* signature, Match& match);
bool FindUniqueByRtti(
    HMODULE module, const char* typeName, uint32_t subobjectOffset,
    uint32_t slotIndex, Match& match);
bool SwapSlot(
    uintptr_t slot, uintptr_t expected, void* replacement);

} // namespace VtableLocator
