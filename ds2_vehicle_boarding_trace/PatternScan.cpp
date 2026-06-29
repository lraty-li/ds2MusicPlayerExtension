#include "pch.h"

#include "PatternScan.h"

#include <cstdlib>
#include <vector>

namespace PatternScan
{
bool GetSection(HMODULE module, const char* name, uintptr_t& start, size_t& size)
{
    start = 0;
    size = 0;
    if (!module || !name)
    {
        return false;
    }

    const auto base = reinterpret_cast<uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if (memcmp(section->Name, name, strlen(name)) == 0)
        {
            start = base + section->VirtualAddress;
            size = section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

uintptr_t Find(uintptr_t base, size_t size, const char* pattern)
{
    struct BytePattern
    {
        uint8_t value;
        bool wildcard;
    };

    std::vector<BytePattern> bytes;
    for (size_t i = 0; pattern && pattern[i] != '\0';)
    {
        while (pattern[i] == ' ')
        {
            ++i;
        }
        if (pattern[i] == '\0')
        {
            break;
        }
        if (pattern[i] == '?')
        {
            bytes.push_back({0, true});
            ++i;
            if (pattern[i] == '?')
            {
                ++i;
            }
        }
        else
        {
            bytes.push_back({static_cast<uint8_t>(strtoul(&pattern[i], nullptr, 16)), false});
            i += 2;
        }
    }

    if (bytes.empty() || size < bytes.size())
    {
        return 0;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(base);
    for (size_t i = 0; i <= size - bytes.size(); ++i)
    {
        bool matched = true;
        for (size_t j = 0; j < bytes.size(); ++j)
        {
            if (!bytes[j].wildcard && data[i + j] != bytes[j].value)
            {
                matched = false;
                break;
            }
        }
        if (matched)
        {
            return base + i;
        }
    }
    return 0;
}

uintptr_t ResolveRip(uintptr_t instruction, uint32_t operandOffset)
{
    const int32_t relative = *reinterpret_cast<int32_t*>(instruction + operandOffset);
    return instruction + operandOffset + sizeof(relative) + relative;
}
}
