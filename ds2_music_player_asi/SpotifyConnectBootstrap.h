#pragma once

#include <windows.h>

class Logger;

namespace SpotifyConnectBootstrap
{
void Start(HMODULE selfModule, const Logger& logger);
}
