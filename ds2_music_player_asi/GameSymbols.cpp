#include "pch.h"

#include "GameSymbols.h"

#include "HookUtils.h"
#include "PatternScan.h"

#include <sstream>

namespace
{
constexpr const char* kRegisterPluginDllExport =
    "?RegisterPluginDLL@SoundEngine@AK@@YA?AW4AKRESULT@@PEB_W0@Z";

const char* kRegisterPluginDllPattern =
    "48 81 EC 28 08 00 00 33 C0 4C 8B CA 4C 8B C1 "
    "66 89 44 24 20 BA 00 04 00 00 48 8D 4C 24 20 "
    "E8 ?? ?? ?? ?? 48 8D 4C 24 20 FF 15 ?? ?? ?? ?? "
    "48 85 C0 75 49 FF 15 ?? ?? ?? ?? 83 E8 08 74 31 "
    "83 E8 76 74 1F 83 F8 43 74 0D B8 02 00 00 00";

bool IsInsideText(HMODULE module, uintptr_t address)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(module, ".text", textStart, textSize))
    {
        return false;
    }

    const uintptr_t textEnd = textStart + static_cast<uintptr_t>(textSize);
    return address >= textStart && address < textEnd;
}

void LogResolved(const Logger& logger, const char* name, const char* source,
    HMODULE gameModule, uintptr_t address)
{
    std::ostringstream oss;
    oss << name << " resolved by " << source
        << " rva=" << HookUtils::HexU64(address - reinterpret_cast<uintptr_t>(gameModule))
        << " address=" << HookUtils::HexU64(address);
    logger.Log(oss.str());
}

GameSymbols::RegisterPluginDllFn ResolveRegisterPluginDllExport(
    HMODULE gameModule, const Logger& logger)
{
    auto* proc = GetProcAddress(gameModule, kRegisterPluginDllExport);
    const uintptr_t address = reinterpret_cast<uintptr_t>(proc);
    if (!proc)
    {
        logger.Log("RegisterPluginDLL export not found");
        return nullptr;
    }

    if (!IsInsideText(gameModule, address))
    {
        logger.Log("RegisterPluginDLL export rejected: address outside .text");
        return nullptr;
    }

    LogResolved(logger, "RegisterPluginDLL", "export", gameModule, address);
    return reinterpret_cast<GameSymbols::RegisterPluginDllFn>(proc);
}

GameSymbols::RegisterPluginDllFn ResolveRegisterPluginDllPattern(
    HMODULE gameModule, const Logger& logger)
{
    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        logger.Log("RegisterPluginDLL pattern skipped: .text not found");
        return nullptr;
    }

    const uintptr_t first = PatternScan::Find(textStart, textSize, kRegisterPluginDllPattern);
    if (!first)
    {
        logger.Log("RegisterPluginDLL pattern not found");
        return nullptr;
    }

    const uintptr_t textEnd = textStart + static_cast<uintptr_t>(textSize);
    const size_t remaining = first + 1 < textEnd ? static_cast<size_t>(textEnd - first - 1) : 0;
    const uintptr_t second = remaining
        ? PatternScan::Find(first + 1, remaining, kRegisterPluginDllPattern)
        : 0;
    if (second)
    {
        logger.Log("RegisterPluginDLL pattern rejected: multiple matches");
        return nullptr;
    }

    LogResolved(logger, "RegisterPluginDLL", "signature", gameModule, first);
    return reinterpret_cast<GameSymbols::RegisterPluginDllFn>(first);
}
} // namespace

namespace GameSymbols
{
ResolvedSymbols Resolve(HMODULE gameModule, const Logger& logger)
{
    ResolvedSymbols symbols;
    if (!gameModule)
    {
        logger.Log("game symbol resolve skipped: missing game module");
        return symbols;
    }

    symbols.registerPluginDll = ResolveRegisterPluginDllExport(gameModule, logger);
    if (!symbols.registerPluginDll)
    {
        symbols.registerPluginDll = ResolveRegisterPluginDllPattern(gameModule, logger);
    }
    return symbols;
}
} // namespace GameSymbols
