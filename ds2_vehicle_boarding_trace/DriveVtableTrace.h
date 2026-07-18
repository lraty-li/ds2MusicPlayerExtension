#pragma once

#include "Logger.h"

#include <windows.h>

namespace DriveVtableTrace {

bool TryInstall(HMODULE gameModule, const Logger& logger);

} // namespace DriveVtableTrace
