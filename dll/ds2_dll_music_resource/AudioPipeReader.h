#pragma once

#include <cstdint>
#include <windows.h>

namespace AudioPipeReader
{
void Start();
void Stop();
uint32_t Read(float* output, uint32_t frames, uint32_t channels);
}
