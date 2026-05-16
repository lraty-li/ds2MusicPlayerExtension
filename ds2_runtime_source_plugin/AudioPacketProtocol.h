#pragma once

#include <cstdint>

namespace AudioPacketProtocol
{
enum class SampleFormat : uint16_t
{
    Pcm16 = 1,
    Float32 = 2,
};

struct Packet
{
    const uint8_t* payload = nullptr;
    uint32_t payloadBytes = 0;
    uint16_t channels = 0;
    uint32_t frames = 0;
    uint64_t sequence = 0;
    SampleFormat format = SampleFormat::Pcm16;
};

bool TryParse(const uint8_t* data, uint32_t bytes, Packet& packet);
}
