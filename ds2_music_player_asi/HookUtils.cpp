#include "pch.h"

#include "HookUtils.h"

#include <sstream>

namespace HookUtils
{
std::wstring GetModulePath(HMODULE module)
{
    wchar_t path[MAX_PATH] = {};
    if (!module)
    {
        return {};
    }

    if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
    {
        return {};
    }

    return path;
}

std::string NarrowUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
    {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], size, nullptr, nullptr);
    result.resize(static_cast<size_t>(size - 1));
    return result;
}

std::string HexU64(uint64_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase << value;
    return oss.str();
}

bool TryGetModuleSize(HMODULE module, DWORD& sizeOfImage)
{
    sizeOfImage = 0;
    if (!module)
    {
        return false;
    }

    const auto base = reinterpret_cast<const unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        return false;
    }

    sizeOfImage = nt->OptionalHeader.SizeOfImage;
    return true;
}

bool IsAddressRangeInModule(HMODULE module, uintptr_t address, size_t size)
{
    DWORD sizeOfImage = 0;
    if (!TryGetModuleSize(module, sizeOfImage))
    {
        return false;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const uintptr_t end = base + static_cast<uintptr_t>(sizeOfImage);
    const uintptr_t rangeEnd = address + static_cast<uintptr_t>(size);

    return address >= base && rangeEnd >= address && rangeEnd <= end;
}
} // namespace HookUtils
