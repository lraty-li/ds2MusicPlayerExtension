#pragma once

#include <cstdint>

namespace AudioStreamServer
{
void Start();
void Stop();
uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels);
bool SendControl(const char* json);
int ReadMetadataTitle(char* output, uint32_t outputBytes);
int ReadMetadata(char* title, uint32_t titleBytes, char* artist, uint32_t artistBytes);
}
