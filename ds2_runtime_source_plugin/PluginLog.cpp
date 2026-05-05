#include "pch.h"

#include "PluginLog.h"

#include <mutex>
#include <string>

namespace
{
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
}

namespace PluginLog
{
void Write(const char* text)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE file = CreateFileW(
        GetLogPath().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
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
    std::lock_guard<std::mutex> lock(g_logMutex);
    HANDLE file = CreateFileW(
        GetLogPath().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }
}
}
