#pragma once

#include <cstddef>

namespace SpecialTrackHelpers
{
void* HeapAllocZero(size_t size);
void ResetObjectHeader(void* object);
void* CreateLocalizedText(const char* text, void* sourceText);
const char* ReadTrackTitle(void* track);
bool ContainsPopVirus(const char* title);
} // namespace SpecialTrackHelpers
