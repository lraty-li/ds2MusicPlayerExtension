#pragma once

#include "Logger.h"

#include <windows.h>

namespace FailFast
{
[[noreturn]] inline void Now(const Logger& logger, const char* reason)
{
    logger.Log(std::string("fatal: ") + (reason ? reason : "unknown"));
    RaiseFailFastException(nullptr, nullptr, 0);
    TerminateProcess(GetCurrentProcess(), 1);
}
}
