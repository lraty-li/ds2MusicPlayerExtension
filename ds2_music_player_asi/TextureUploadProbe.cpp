#include "pch.h"

#include "TextureUploadProbe.h"

#include "HookUtils.h"
#include "TextureUploadHistory.h"

#include <intrin.h>
#include <sstream>

namespace
{
constexpr uintptr_t kUploadTexturePayloadRva = 0x2113810;
constexpr uint32_t kPatchBytes = 14;
constexpr LONG kDetailedLogLimit = 48;

using UploadFn = void(__fastcall*)(uint64_t textureDx12, uint64_t reader);

Logger* g_logger = nullptr;
UploadFn g_original = nullptr;
LONG g_callCount = 0;

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

bool Read32(uint64_t addr, uint32_t& out)
{
    __try
    {
        out = *reinterpret_cast<uint32_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = 0;
        return false;
    }
}

std::string H(uint64_t value)
{
    return HookUtils::HexU64(value);
}

void WriteAbsoluteJump(uint8_t* target, void* destination)
{
    target[0] = 0x48;
    target[1] = 0xB8;
    *reinterpret_cast<void**>(target + 2) = destination;
    target[10] = 0xFF;
    target[11] = 0xE0;
}

bool InstallDetour(uintptr_t target, void* detour, void** original)
{
    auto* gateway = static_cast<uint8_t*>(VirtualAlloc(nullptr, kPatchBytes + 12,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!gateway) return false;

    memcpy(gateway, reinterpret_cast<void*>(target), kPatchBytes);
    WriteAbsoluteJump(gateway + kPatchBytes, reinterpret_cast<void*>(target + kPatchBytes));

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), kPatchBytes,
        PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        return false;
    }

    auto* patch = reinterpret_cast<uint8_t*>(target);
    WriteAbsoluteJump(patch, detour);
    for (uint32_t i = 12; i < kPatchBytes; ++i) patch[i] = 0x90;
    VirtualProtect(patch, kPatchBytes, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), patch, kPatchBytes);
    *original = reinterpret_cast<UploadFn>(gateway);
    return true;
}

void LogVtableSlots(const char* label, uint64_t object)
{
    uint64_t vt = 0;
    uint64_t s0 = 0;
    uint64_t s8 = 0;
    uint64_t s10 = 0;
    uint64_t s18 = 0;
    uint64_t s20 = 0;
    Read64(object, vt);
    if (vt)
    {
        Read64(vt + 0x00, s0);
        Read64(vt + 0x08, s8);
        Read64(vt + 0x10, s10);
        Read64(vt + 0x18, s18);
        Read64(vt + 0x20, s20);
    }

    std::ostringstream oss;
    oss << "txupload vt " << label
        << " obj=" << H(object)
        << " vt=" << H(vt)
        << " s0=" << H(s0)
        << " s8=" << H(s8)
        << " s10=" << H(s10)
        << " s18=" << H(s18)
        << " s20=" << H(s20);
    Log(oss.str());
}

void LogTextureFields(uint64_t texture)
{
    uint32_t f2c = 0;
    uint32_t f30 = 0;
    uint32_t f60 = 0;
    uint32_t f64 = 0;
    uint64_t header = 0;
    uint64_t slot78Obj = 0;
    uint64_t slot88Wrap = 0;
    uint64_t desc90 = 0;
    uint64_t slotC8Obj = 0;
    uint64_t slotD8Wrap = 0;
    uint64_t descE0 = 0;
    Read64(texture + 0x40, header);
    Read32(texture + 0x2C, f2c);
    Read32(texture + 0x30, f30);
    Read32(texture + 0x60, f60);
    Read32(texture + 0x64, f64);
    Read64(texture + 0x80, slot78Obj);
    Read64(texture + 0x88, slot88Wrap);
    Read64(texture + 0x90, desc90);
    Read64(texture + 0xD0, slotC8Obj);
    Read64(texture + 0xD8, slotD8Wrap);
    Read64(texture + 0xE0, descE0);

    std::ostringstream oss;
    oss << "txupload tex"
        << " hdr=" << H(header)
        << " f2c=" << H(f2c)
        << " f30=" << H(f30)
        << " f60=" << H(f60)
        << " f64=" << H(f64)
        << " s80=" << H(slot78Obj)
        << " s88=" << H(slot88Wrap)
        << " d90=" << H(desc90)
        << " sD0=" << H(slotC8Obj)
        << " sD8=" << H(slotD8Wrap)
        << " dE0=" << H(descE0);
    Log(oss.str());
}

void LogReaderWords(uint64_t reader)
{
    uint64_t q[8] = {};
    for (uint32_t i = 0; i < 8; ++i)
    {
        Read64(reader + i * 8ull, q[i]);
    }

    std::ostringstream oss;
    oss << "txupload reader q0=" << H(q[0])
        << " q8=" << H(q[1])
        << " q10=" << H(q[2])
        << " q18=" << H(q[3])
        << " q20=" << H(q[4])
        << " q28=" << H(q[5])
        << " q30=" << H(q[6])
        << " q38=" << H(q[7]);
    Log(oss.str());
}

TextureUploadHistory::Snapshot CaptureSnapshot(LONG count,
    uint64_t textureDx12, uint64_t reader, uint64_t callerRva)
{
    TextureUploadHistory::Snapshot s = {};
    s.callIndex = static_cast<uint64_t>(count);
    s.textureDx12 = textureDx12;
    s.reader = reader;
    s.callerRva = callerRva;
    Read64(reader, s.readerVtable);
    Read64(textureDx12 + 0x40, s.header);
    Read32(textureDx12 + 0x2C, s.field2C);
    Read32(textureDx12 + 0x30, s.field30);
    Read32(textureDx12 + 0x60, s.field60);
    Read32(textureDx12 + 0x64, s.field64);
    Read64(textureDx12 + 0x80, s.slot80);
    Read64(textureDx12 + 0x88, s.slot88);
    Read64(textureDx12 + 0x90, s.desc90);
    Read64(textureDx12 + 0xD0, s.slotD0);
    Read64(textureDx12 + 0xD8, s.slotD8);
    Read64(textureDx12 + 0xE0, s.descE0);
    Read64(reader + 0x08, s.readerQ8);
    Read64(reader + 0x10, s.readerQ10);
    Read64(reader + 0x18, s.readerQ18);
    Read64(reader + 0x20, s.readerQ20);
    Read64(reader + 0x28, s.readerQ28);
    Read64(reader + 0x30, s.readerQ30);
    Read64(reader + 0x38, s.readerQ38);
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(reinterpret_cast<void*>(reader), &mbi, sizeof(mbi)))
    {
        s.readerRegionBase = reinterpret_cast<uint64_t>(mbi.BaseAddress);
        s.readerRegionSize = static_cast<uint64_t>(mbi.RegionSize);
        s.readerProtect = mbi.Protect;
        s.readerType = mbi.Type;
        s.readerState = mbi.State;
    }
    ULONG_PTR stackLow = 0;
    ULONG_PTR stackHigh = 0;
    GetCurrentThreadStackLimits(&stackLow, &stackHigh);
    s.threadId = GetCurrentThreadId();
    s.stackLow = static_cast<uint64_t>(stackLow);
    s.stackHigh = static_cast<uint64_t>(stackHigh);
    s.readerOnStack = (reader >= s.stackLow && reader < s.stackHigh) ? 1u : 0u;
    return s;
}

void __fastcall DetourUpload(uint64_t textureDx12, uint64_t reader)
{
    const LONG count = InterlockedIncrement(&g_callCount);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const TextureUploadHistory::Snapshot snapshot =
        CaptureSnapshot(count, textureDx12, reader, caller - base);
    TextureUploadHistory::Record(snapshot);
    if (count <= kDetailedLogLimit)
    {
        std::ostringstream oss;
        oss << "txupload call=" << count
            << " tex=" << H(textureDx12)
            << " reader=" << H(reader)
            << " callerRva=" << H(snapshot.callerRva);
        Log(oss.str());
        LogVtableSlots("reader", reader);
        LogVtableSlots("tex", textureDx12);
        LogTextureFields(textureDx12);
        LogReaderWords(reader);
    }

    g_original(textureDx12, reader);
}
} // namespace

namespace TextureUploadProbe
{
bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    if (!gameModule) return false;

    const uintptr_t target = reinterpret_cast<uintptr_t>(gameModule) + kUploadTexturePayloadRva;
    if (!HookUtils::IsAddressRangeInModule(gameModule, target, kPatchBytes))
    {
        Log("txupload install skipped: target outside module");
        return false;
    }

    if (!InstallDetour(target, reinterpret_cast<void*>(&DetourUpload),
        reinterpret_cast<void**>(&g_original)))
    {
        Log("txupload install skipped: detour failed");
        return false;
    }

    Log("txupload installed at rva=" + H(kUploadTexturePayloadRva));
    return true;
}
} // namespace TextureUploadProbe
