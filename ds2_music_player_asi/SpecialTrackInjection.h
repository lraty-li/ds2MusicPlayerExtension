#pragma once

#include "DecimaTypes.h"
#include "Logger.h"

namespace SpecialTrackInjection
{
void Reset();
void Inject(void* systemResource, const Logger& logger);
}
