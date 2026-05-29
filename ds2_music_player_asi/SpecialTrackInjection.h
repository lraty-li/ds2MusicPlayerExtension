#pragma once

#include "DecimaTypes.h"
#include "Logger.h"

namespace SpecialTrackInjection
{
void Reset();
bool Inject(void* systemResource, const Logger& logger);
}
