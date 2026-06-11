#include "pch.h"

#include "TextureDx12BindResolver.h"

#include "HookUtils.h"
#include "PatternScan.h"

#include <sstream>

namespace
{
const char* kBindProloguePattern =
    "40 53 56 57 48 81 EC 90 00 00 00 "
    "48 8B 05 ?? ?? ?? ?? 48 33 C4";

bool MatchBytes(const uint8_t* data, const uint8_t* pattern, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (data[i] != pattern[i])
        {
            return false;
        }
    }
    return true;
}

bool ContainsBytes(uintptr_t start, size_t size, const uint8_t* pattern, size_t patternSize)
{
    if (!start || size < patternSize)
    {
        return false;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(start);
    __try
    {
        for (size_t i = 0; i <= size - patternSize; ++i)
        {
            if (MatchBytes(data + i, pattern, patternSize))
            {
                return true;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return false;
}

bool ValidateBindFunction(uintptr_t candidate)
{
    const uint8_t copyMainHandle[] = {
        0x49, 0x8D, 0x4D, 0x78, 0xE8
    };
    const uint8_t createMainViews[] = {
        0x49, 0x8D, 0x55, 0x78, 0x49, 0x8B, 0xCD, 0xE8
    };
    return ContainsBytes(candidate, 0x700,
        copyMainHandle, sizeof(copyMainHandle)) &&
        ContainsBytes(candidate, 0x900,
            createMainViews, sizeof(createMainViews));
}

void LogResolved(uintptr_t base, uintptr_t addr, const Logger& logger)
{
    std::ostringstream oss;
    oss << "TextureDX12 bind resolved by signature rva="
        << HookUtils::HexU64(addr - base)
        << " address=" << HookUtils::HexU64(addr);
    logger.Log(oss.str());
}
} // namespace

namespace TextureDx12BindResolver
{
bool Resolve(HMODULE gameModule, BindFn& bindFn, const Logger& logger)
{
    bindFn = nullptr;
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        logger.Log("TextureDX12 bind unresolved: .text not found");
        return false;
    }

    uintptr_t searchStart = textStart;
    size_t remaining = textSize;
    const uintptr_t base = reinterpret_cast<uintptr_t>(gameModule);
    while (remaining > 0)
    {
        const uintptr_t candidate = PatternScan::Find(
            searchStart, remaining, kBindProloguePattern);
        if (!candidate)
        {
            break;
        }
        if (ValidateBindFunction(candidate))
        {
            bindFn = reinterpret_cast<BindFn>(candidate);
            LogResolved(base, candidate, logger);
            return true;
        }

        const uintptr_t next = candidate + 1;
        remaining = textStart + textSize > next ? textStart + textSize - next : 0;
        searchStart = next;
    }

    logger.Log("TextureDX12 bind unresolved: signature not found");
    return false;
}
} // namespace TextureDx12BindResolver
