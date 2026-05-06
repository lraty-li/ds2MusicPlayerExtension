#pragma once

#include "Logger.h"

namespace DynamicTrackTitleSync
{
void Reset();
void Start(void* track, void* album, const Logger& logger);
}
