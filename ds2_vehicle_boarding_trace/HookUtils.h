#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace HookUtils
{
std::wstring GetModulePath(HMODULE module);
std::string NarrowUtf8(const std::wstring& value);
std::string HexU64(uint64_t value);
bool TryGetModuleSize(HMODULE module, DWORD& sizeOfImage);
bool IsAddressRangeInModule(HMODULE module, uintptr_t address, size_t size);
} // namespace HookUtils
