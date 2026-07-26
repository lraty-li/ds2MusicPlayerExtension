#include "pch.h"

#include "PluginLog.h"

#include <mutex>
#include <string>

namespace
{
constexpr bool kEnableRuntimeLog = true;

std::mutex g_logMutex;

std::wstring GetLogPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    std::wstring result = path;
    const size_t pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        result.resize(pos + 1);
    }

    result += L"ds2_dll_music_resource.log";
    return result;
}

HANDLE OpenLogFile(DWORD access, DWORD creation)
{
    HANDLE file = CreateFileW(
        GetLogPath().c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        creation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) return file;

    wchar_t currentDirectory[MAX_PATH] = {};
    if (!GetCurrentDirectoryW(MAX_PATH, currentDirectory))
    {
        return INVALID_HANDLE_VALUE;
    }
    std::wstring fallback = currentDirectory;
    fallback += L"\\ds2_dll_music_resource.log";
    return CreateFileW(
        fallback.c_str(),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        creation,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}
}

namespace PluginLog
{
bool Enabled()
{
    return kEnableRuntimeLog;
}

void Write(const char* text)
{
    if (!kEnableRuntimeLog) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE file = OpenLogFile(
        FILE_APPEND_DATA,
        OPEN_ALWAYS);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char prefix[96] = {};
    wsprintfA(prefix, "[%02u:%02u:%02u.%03u][tid=%lu] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, GetCurrentThreadId());

    DWORD written = 0;
    WriteFile(file, prefix, static_cast<DWORD>(lstrlenA(prefix)), &written, nullptr);
    WriteFile(file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

void Reset()
{
    if (!kEnableRuntimeLog) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE file = OpenLogFile(
        GENERIC_WRITE,
        CREATE_ALWAYS);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }
}
}
