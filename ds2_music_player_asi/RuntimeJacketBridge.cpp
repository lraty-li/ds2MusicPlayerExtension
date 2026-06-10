#include "pch.h"

#include "RuntimeJacketBridge.h"

namespace
{
using ReadInfoFn = int(__cdecl*)(unsigned int*, unsigned int*, char*, unsigned int);
using ReadBytesFn = int(__cdecl*)(unsigned int, void*, unsigned int,
    unsigned int*, char*, unsigned int);
using ReadStatusFn = int(__cdecl*)(unsigned int*, char*, unsigned int,
    unsigned int*, char*, unsigned int, char*, unsigned int,
    char*, unsigned int, char*, unsigned int);

ReadInfoFn g_readInfo = nullptr;
ReadBytesFn g_readBytes = nullptr;
ReadStatusFn g_readStatus = nullptr;

bool ResolveExports()
{
    HMODULE module = GetModuleHandleW(L"ds2_dll_music_resource.dll");
    if (!module) return false;
    g_readInfo = reinterpret_cast<ReadInfoFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketInfo"));
    g_readBytes = reinterpret_cast<ReadBytesFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketBytes"));
    g_readStatus = reinterpret_cast<ReadStatusFn>(
        GetProcAddress(module, "DS2AudioStreamReadJacketStatus"));
    return g_readInfo && g_readBytes;
}
} // namespace

namespace RuntimeJacketBridge
{
bool EnsureReady()
{
    return (g_readInfo && g_readBytes) || ResolveExports();
}

bool ReadInfo(JacketInfo& info)
{
    if (!EnsureReady()) return false;
    return g_readInfo(&info.version, &info.bytes, info.mime, sizeof(info.mime)) != 0;
}

int ReadBytes(unsigned int knownVersion, void* out, unsigned int outBytes,
    unsigned int& written, char* mime, unsigned int mimeBytes)
{
    written = 0;
    if (!EnsureReady()) return 0;
    return g_readBytes(knownVersion, out, outBytes, &written, mime, mimeBytes);
}

bool ReadStatus(JacketStatus& status)
{
    if (!EnsureReady() || !g_readStatus) return false;
    return g_readStatus(&status.version, status.stage, sizeof(status.stage),
        &status.bytes, status.mime, sizeof(status.mime), status.error,
        sizeof(status.error), status.source, sizeof(status.source),
        status.jacketSource, sizeof(status.jacketSource)) != 0;
}
} // namespace RuntimeJacketBridge
