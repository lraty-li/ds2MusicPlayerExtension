#include "pch.h"

#include "TextureBindProbe.h"

#include "HookUtils.h"

#include <sstream>

namespace
{
constexpr uintptr_t kBindResourceHandleRva = 0x2116B40;
constexpr uint32_t kPatchBytes = 18;
constexpr LONG kEventLimit = 64;

struct BindEvent
{
    volatile LONG ready = 0;
    uint64_t texture = 0;
    uint64_t slot = 0;
    uint64_t caller = 0;
};

Logger* g_logger = nullptr;
BindEvent g_events[kEventLimit] = {};
volatile LONG g_nextEvent = 0;

std::string H(uint64_t value)
{
    return HookUtils::HexU64(value);
}

void Log(const std::string& text)
{
    if (g_logger) g_logger->Log(text);
}

bool Read64(uint64_t addr, uint64_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint64_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = 0;
        return false;
    }
}

uint64_t Read64OrZero(uint64_t addr)
{
    uint64_t value = 0;
    Read64(addr, value);
    return value;
}

void WriteAbsoluteJump(uint8_t* target, void* destination)
{
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = destination;
    target[10] = 0xFF;
    target[11] = 0xE0;
}

bool BuildGateway(uintptr_t target, void** gatewayOut)
{
    auto* gateway = static_cast<uint8_t*>(VirtualAlloc(nullptr, 40,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!gateway) return false;

    auto* src = reinterpret_cast<uint8_t*>(target);
    const uint8_t expected[] = {
        0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05
    };
    if (memcmp(src, expected, sizeof(expected)) != 0)
    {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return false;
    }

    memcpy(gateway, src, 11);
    auto* cursor = gateway + 11;
    const auto disp = *reinterpret_cast<int32_t*>(src + 14);
    const auto cookieAddr = target + kPatchBytes + disp;
    cursor[0] = 0x48;
    cursor[1] = 0xA1;
    *reinterpret_cast<uint64_t*>(cursor + 2) = cookieAddr;
    cursor += 10;
    WriteAbsoluteJump(cursor, reinterpret_cast<void*>(target + kPatchBytes));
    *gatewayOut = gateway;
    return true;
}

extern "C" void __fastcall TextureBindRecord(uint64_t texture, uint64_t slot, uint64_t caller)
{
    const LONG rawIndex = InterlockedIncrement(&g_nextEvent) - 1;
    if (rawIndex < 0 || rawIndex >= kEventLimit) return;

    auto& event = g_events[rawIndex];
    event.texture = texture;
    event.slot = slot;
    event.caller = caller;
    InterlockedExchange(&event.ready, 1);
}

uint8_t* EmitEntryStub(void* recordFn, void* gateway)
{
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 256,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!code) return nullptr;

    uint32_t i = 0;
    auto b = [&](uint8_t v) { code[i++] = v; };
    auto imm32 = [&](uint32_t v) { memcpy(code + i, &v, 4); i += 4; };
    auto imm64 = [&](uint64_t v) { memcpy(code + i, &v, 8); i += 8; };
    auto pushR = [&](uint8_t low) { b(low); };
    auto pushX = [&](uint8_t low) { b(0x41); b(low); };
    auto popR = [&](uint8_t low) { b(low); };
    auto popX = [&](uint8_t low) { b(0x41); b(low); };

    b(0x9C);
    pushR(0x50); pushR(0x51); pushR(0x52);
    pushX(0x50); pushX(0x51); pushX(0x52); pushX(0x53);
    pushR(0x53); pushR(0x55); pushR(0x56); pushR(0x57);
    pushX(0x54); pushX(0x55); pushX(0x56); pushX(0x57);
    b(0x48); b(0x83); b(0xEC); b(0x28);
    b(0x48); b(0x8B); b(0x8C); b(0x24); imm32(0x90);
    b(0x48); b(0x8B); b(0x94); b(0x24); imm32(0x88);
    b(0x4C); b(0x8B); b(0x84); b(0x24); imm32(0xA8);
    b(0x48); b(0xB8); imm64(reinterpret_cast<uint64_t>(recordFn));
    b(0xFF); b(0xD0);
    b(0x48); b(0x83); b(0xC4); b(0x28);
    popX(0x5F); popX(0x5E); popX(0x5D); popX(0x5C);
    popR(0x5F); popR(0x5E); popR(0x5D); popR(0x5B);
    popX(0x5B); popX(0x5A); popX(0x59); popX(0x58);
    popR(0x5A); popR(0x59); popR(0x58);
    b(0x9D);
    b(0x48); b(0xB8); imm64(reinterpret_cast<uint64_t>(gateway));
    b(0xFF); b(0xE0);
    FlushInstructionCache(GetCurrentProcess(), code, i);
    return code;
}

void LogEvent(uint32_t index, const BindEvent& event)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uint64_t tex = event.texture;
    const uint64_t slot = event.slot;

    std::ostringstream oss;
    oss << "txbind event=" << index
        << " tex=" << H(tex)
        << " slot=" << H(slot)
        << " callerRva=" << H(event.caller - base)
        << " tex.s88=" << H(Read64OrZero(tex + 0x88))
        << " tex.d90=" << H(Read64OrZero(tex + 0x90))
        << " tex.sD8=" << H(Read64OrZero(tex + 0xD8))
        << " tex.dE0=" << H(Read64OrZero(tex + 0xE0))
        << " slot.q0=" << H(Read64OrZero(slot))
        << " slot.resource=" << H(Read64OrZero(slot + 0x08))
        << " slot.wrapper=" << H(Read64OrZero(slot + 0x10));
    Log(oss.str());
}

DWORD WINAPI FlushThread(LPVOID)
{
    Sleep(1500);
    LONG flushed = 0;
    for (uint32_t pass = 0; pass < 40; ++pass)
    {
        const LONG count = g_nextEvent < kEventLimit ? g_nextEvent : kEventLimit;
        while (flushed < count)
        {
            auto& event = g_events[flushed];
            if (event.ready) LogEvent(static_cast<uint32_t>(flushed), event);
            ++flushed;
        }
        Sleep(250);
    }
    return 0;
}
} // namespace

namespace TextureBindProbe
{
bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    if (!gameModule) return false;

    const uintptr_t target = reinterpret_cast<uintptr_t>(gameModule) + kBindResourceHandleRva;
    if (!HookUtils::IsAddressRangeInModule(gameModule, target, kPatchBytes))
    {
        Log("txbind install skipped: target outside module");
        return false;
    }

    void* gateway = nullptr;
    if (!BuildGateway(target, &gateway))
    {
        Log("txbind install skipped: gateway failed");
        return false;
    }

    auto* stub = EmitEntryStub(reinterpret_cast<void*>(&TextureBindRecord), gateway);
    if (!stub)
    {
        Log("txbind install skipped: stub failed");
        return false;
    }

    DWORD oldProtect = 0;
    auto* patch = reinterpret_cast<uint8_t*>(target);
    if (!VirtualProtect(patch, kPatchBytes, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        return false;
    }
    WriteAbsoluteJump(patch, stub);
    for (uint32_t i = 12; i < kPatchBytes; ++i) patch[i] = 0x90;
    VirtualProtect(patch, kPatchBytes, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), patch, kPatchBytes);

    CreateThread(nullptr, 0, FlushThread, nullptr, 0, nullptr);
    Log("txbind installed at rva=" + H(kBindResourceHandleRva));
    Log("txbind memory recorder installed bytes=18");
    return true;
}
} // namespace TextureBindProbe
