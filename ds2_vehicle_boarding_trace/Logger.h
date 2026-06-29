#pragma once

#include <string>
#include <windows.h>

class Logger
{
public:
    explicit Logger(const char* tag);

    void Log(const std::string& message) const;

    static std::wstring GetLogPath();
    static bool ResetLogFile(DWORD& lastError);

private:
    std::string tag_;
};
