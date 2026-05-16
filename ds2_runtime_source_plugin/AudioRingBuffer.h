#pragma once

#include <cstdint>

namespace AudioRingBuffer
{
void PushPcm16(const uint8_t* pcm, uint32_t frames, uint16_t channels);
uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels);
uint32_t AvailableFrames();
uint64_t Underruns();
}
