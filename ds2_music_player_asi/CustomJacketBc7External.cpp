#include "pch.h"

#include "CustomJacketInternal.h"

#include <mutex>
#include <sstream>
#include <vector>

namespace
{
using EncodeFn = int(__cdecl*)(const uint8_t*, uint32_t, uint32_t, uint8_t*, uint32_t);
using VersionFn = uint32_t(__cdecl*)();

std::mutex g_mutex;
bool g_resolved = false;
HMODULE g_module = nullptr;
EncodeFn g_encode = nullptr;
VersionFn g_version = nullptr;

std::wstring DllPath()
{
    wchar_t path[MAX_PATH] = {};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&DllPath), &self);
    if (!self || !GetModuleFileNameW(self, path, MAX_PATH))
    {
        return L"ds2_jacket_bc7e.dll";
    }
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return L"ds2_jacket_bc7e.dll";
    slash[1] = 0;
    return std::wstring(path) + L"ds2_jacket_bc7e.dll";
}

std::string NarrowPath(const std::wstring& path)
{
    char out[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, out, sizeof(out), nullptr, nullptr);
    return out;
}

HMODULE LoadEncoderDll(const std::wstring& path, std::wstring& loadedPath,
    DWORD& primaryError, DWORD& fallbackError)
{
    loadedPath = path;
    SetLastError(0);
    HMODULE module = LoadLibraryW(path.c_str());
    primaryError = GetLastError();
    if (module) return module;

    loadedPath = L"ds2_jacket_bc7e.dll";
    SetLastError(0);
    module = LoadLibraryW(loadedPath.c_str());
    fallbackError = GetLastError();
    return module;
}

void ResolveEncoder(const Logger& logger)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_resolved) return;
    g_resolved = true;

    const std::wstring path = DllPath();
    std::wstring loadedPath;
    DWORD primaryError = 0;
    DWORD fallbackError = 0;
    g_module = LoadEncoderDll(path, loadedPath, primaryError, fallbackError);
    if (!g_module)
    {
        std::ostringstream oss;
        oss << "bc7e external unavailable path=\"" << NarrowPath(path)
            << "\" error=" << primaryError << " fallbackError=" << fallbackError;
        logger.Log(oss.str());
        return;
    }

    g_encode = reinterpret_cast<EncodeFn>(GetProcAddress(g_module, "DS2_EncodeRgbaToBc7"));
    if (!g_encode)
    {
        std::ostringstream oss;
        oss << "bc7e external missing export DS2_EncodeRgbaToBc7 path=\""
            << NarrowPath(loadedPath) << "\" error=" << GetLastError();
        logger.Log(oss.str());
        return;
    }

    g_version = reinterpret_cast<VersionFn>(GetProcAddress(g_module, "DS2_Bc7eWrapperVersion"));
    const uint32_t version = g_version ? g_version() : 0;
    std::ostringstream oss;
    oss << "bc7e external loaded version=" << version
        << " path=\"" << NarrowPath(loadedPath) << "\"";
    logger.Log(oss.str());
}

void CopyPackedRows(uint8_t* dst, uint32_t rowPitch, const uint8_t* src,
    uint32_t rowBytes, uint32_t rows)
{
    for (uint32_t y = 0; y < rows; ++y)
    {
        memcpy(dst + uint64_t(y) * rowPitch, src + uint64_t(y) * rowBytes, rowBytes);
    }
}
}

namespace CustomJacketInternal
{
bool TryEncodeExternalBc7ToRows(uint8_t* dst, uint32_t dstW, uint32_t dstH,
    uint32_t rowPitch, const uint8_t* rgba, uint32_t srcW, uint32_t srcH,
    const Logger& logger)
{
    if (!dst || !rgba)
    {
        logger.Log("bc7e external skipped invalid buffer");
        return false;
    }
    if (dstW != srcW || dstH != srcH)
    {
        std::ostringstream oss;
        oss << "bc7e external skipped size mismatch dst=" << dstW << "x" << dstH
            << " src=" << srcW << "x" << srcH;
        logger.Log(oss.str());
        return false;
    }
    ResolveEncoder(logger);
    if (!g_encode) return false;

    const uint32_t blockRows = (dstH + 3) / 4;
    const uint32_t rowBytes = ((dstW + 3) / 4) * 16;
    const uint32_t bc7Bytes = rowBytes * blockRows;
    if (rowPitch == rowBytes)
    {
        const bool ok = g_encode(rgba, srcW, srcH, dst, bc7Bytes) != 0;
        if (!ok) logger.Log("bc7e external encode failed direct");
        return ok;
    }

    std::vector<uint8_t> packed(bc7Bytes);
    if (g_encode(rgba, srcW, srcH, packed.data(), bc7Bytes) == 0)
    {
        logger.Log("bc7e external encode failed packed");
        return false;
    }
    CopyPackedRows(dst, rowPitch, packed.data(), rowBytes, blockRows);
    return true;
}
}
