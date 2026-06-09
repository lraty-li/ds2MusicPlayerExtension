#pragma once

#include "Logger.h"

namespace CustomJacketInstaller
{
void Reset();
void TrackCreated(void* track);
void TryApply(void* catalogueResource, const Logger& logger);
}
