#pragma once

#include "Logger.h"

namespace CustomJacketPixelTest
{
void Reset();
void TrackCreated(void* track);
void TryApply(void* catalogueResource, const Logger& logger);
}
