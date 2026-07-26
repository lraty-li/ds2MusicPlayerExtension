#pragma once

#include <cstdint>

namespace BrowserMetadata
{
void UpdateFromJson(const char* json);
bool IsCurrentTrackKey(const char* trackKey);
int ReadTitle(char* output, uint32_t outputBytes);
int Read(char* title, uint32_t titleBytes, char* artist, uint32_t artistBytes);
}
