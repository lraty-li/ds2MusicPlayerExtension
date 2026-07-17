#pragma once

#include <cstdint>
#include <windows.h>

namespace PatternScan
{
bool GetSection(HMODULE module, const char* name, uintptr_t& start, size_t& size);
uintptr_t Find(uintptr_t base, size_t size, const char* pattern);
uintptr_t FindUnique(uintptr_t base, size_t size, const char* pattern);
uintptr_t ResolveRip(uintptr_t instruction, uint32_t operandOffset);
}
