#pragma once

#include <cstdint>

namespace AudioStreamServer
{
void Start();
void Stop();
uint32_t Read(float* output, uint32_t frames, uint32_t channels);
bool SendControl(const char* json);
}
