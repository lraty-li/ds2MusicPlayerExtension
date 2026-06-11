#include "pch.h"

#include "TextSharedStringResolver.h"

#include "PatternScan.h"

namespace
{
const char* kLocalizedTextToUiSharedStringPattern =
    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? "
    "48 8B 05 ? ? ? ? 48 8B F9 48 8D 0D ? ? ? ? 48 8B DA";

const char* kUiSharedStringMoveAssignPattern =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B FA 48 8B D9 "
    "48 8B 11 48 3B 17 0F 84 ? ? ? ? 48 83 C2";

bool FindUnique(uintptr_t textStart, size_t textSize, const char* pattern,
    uintptr_t& address)
{
    address = PatternScan::Find(textStart, textSize, pattern);
    if (!address)
    {
        return false;
    }

    const uintptr_t textEnd = textStart + static_cast<uintptr_t>(textSize);
    const uintptr_t next = address + 1;
    if (next >= textEnd)
    {
        return true;
    }

    const size_t remaining = static_cast<size_t>(textEnd - next);
    return PatternScan::Find(next, remaining, pattern) == 0;
}
}

namespace TextSharedStringResolver
{
bool Resolve(HMODULE gameModule,
    LocalizedTextToUiSharedStringFn& toUiSharedString,
    UiSharedStringMoveAssignFn& moveAssign)
{
    toUiSharedString = nullptr;
    moveAssign = nullptr;

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        return false;
    }

    uintptr_t toUiAddr = 0;
    uintptr_t moveAddr = 0;
    if (!FindUnique(textStart, textSize,
        kLocalizedTextToUiSharedStringPattern, toUiAddr))
    {
        return false;
    }
    if (!FindUnique(textStart, textSize,
        kUiSharedStringMoveAssignPattern, moveAddr))
    {
        return false;
    }

    toUiSharedString = reinterpret_cast<LocalizedTextToUiSharedStringFn>(toUiAddr);
    moveAssign = reinterpret_cast<UiSharedStringMoveAssignFn>(moveAddr);
    return true;
}
}
