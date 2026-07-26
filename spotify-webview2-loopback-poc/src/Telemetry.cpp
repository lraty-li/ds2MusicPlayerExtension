#include "PocApp.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
std::filesystem::path TelemetryPath(
    const std::wstring& folder,
    bool helperMode)
{
    return std::filesystem::path(folder) /
        (helperMode ? L"helper-telemetry.log" : L"standalone-telemetry.log");
}

std::filesystem::path StatusPath(const std::wstring& folder)
{
    return std::filesystem::path(folder) / L"helper-status.log";
}

std::string Utf8(const std::wstring& value)
{
    if (value.empty())
    {
        return {};
    }
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    std::string encoded(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        encoded.data(), length, nullptr, nullptr);
    return encoded;
}

std::string Timestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::ostringstream text;
    text << std::setfill('0')
         << time.wYear << '-'
         << std::setw(2) << time.wMonth << '-'
         << std::setw(2) << time.wDay << 'T'
         << std::setw(2) << time.wHour << ':'
         << std::setw(2) << time.wMinute << ':'
         << std::setw(2) << time.wSecond << '.'
         << std::setw(3) << time.wMilliseconds;
    return text.str();
}

std::wstring SingleLine(std::wstring value)
{
    for (wchar_t& character : value)
    {
        if (character == L'\r' || character == L'\n' || character == L'\t')
        {
            character = L' ';
        }
    }
    return value;
}
}

void PocApp::ResetTelemetry()
{
    if (!diagnosticsEnabled_ || userDataFolder_.empty())
    {
        return;
    }
    std::ofstream output(
        TelemetryPath(userDataFolder_, helperMode_),
        std::ios::binary | std::ios::trunc);
    output << Timestamp() << "\tSESSION\tstart\n";
}

void PocApp::AppendTelemetry(
    const char* source, const std::wstring& payload)
{
    if (!diagnosticsEnabled_ || userDataFolder_.empty() || !source)
    {
        return;
    }
    std::ofstream output(
        TelemetryPath(userDataFolder_, helperMode_),
        std::ios::binary | std::ios::app);
    output << Timestamp() << '\t' << source << '\t'
           << Utf8(SingleLine(payload)) << '\n';
}

void PocApp::ResetStatusLog()
{
    if (!diagnosticsEnabled_ || !helperMode_ || userDataFolder_.empty())
    {
        return;
    }
    std::ofstream output(
        StatusPath(userDataFolder_),
        std::ios::binary | std::ios::trunc);
    output << Timestamp() << "\tstart\n";
}

void PocApp::AppendStatus(const std::wstring& payload)
{
    if (!diagnosticsEnabled_ || !helperMode_ || userDataFolder_.empty())
    {
        return;
    }
    const std::wstring line = SingleLine(payload).substr(0, 512);
    std::ofstream output(
        StatusPath(userDataFolder_),
        std::ios::binary | std::ios::app);
    output << Timestamp() << '\t' << Utf8(line) << '\n';
}
