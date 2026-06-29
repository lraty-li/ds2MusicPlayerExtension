#include "pch.h"

#include "Logger.h"

#include <mutex>

namespace
{
std::mutex g_logMutex;

std::string MakePrefix(const char* tag)
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);

    char buffer[128] = {};
    wsprintfA(
        buffer,
        "[%02u:%02u:%02u.%03u][tid=%lu][%s] ",
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds,
        GetCurrentThreadId(),
        tag ? tag : "log");
    return buffer;
}

void WriteLine(const char* tag, const std::string& line, const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    HANDLE file = CreateFileW(
        path.c_str(),
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

    const std::string payload = MakePrefix(tag) + line + "\r\n";
    DWORD written = 0;
    WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    CloseHandle(file);

    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdoutHandle != nullptr && stdoutHandle != INVALID_HANDLE_VALUE)
    {
        DWORD stdoutWritten = 0;
        WriteFile(
            stdoutHandle,
            payload.data(),
            static_cast<DWORD>(payload.size()),
            &stdoutWritten,
            nullptr);
    }
}
} // namespace

Logger::Logger(const char* tag)
    : tag_(tag ? tag : "log")
{
}

void Logger::Log(const std::string& message) const
{
    WriteLine(tag_.c_str(), message, GetLogPath());
}

std::wstring Logger::GetLogPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    std::wstring fullPath = path;
    const size_t pos = fullPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        fullPath.resize(pos + 1);
    }

    fullPath += L"log.txt";
    return fullPath;
}

bool Logger::ResetLogFile(DWORD& lastError)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    lastError = ERROR_SUCCESS;

    const std::wstring logPath = GetLogPath();
    HANDLE file = CreateFileW(
        logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
    {
        lastError = GetLastError();
        return false;
    }

    CloseHandle(file);
    return true;
}
