#include "pch.h"

#include "AudioPacketProtocol.h"

#include <cstring>

namespace
{
constexpr uint32_t kMagic = 0x44533241;
constexpr uint16_t kVersionPcm16 = 1;
constexpr uint16_t kVersionFloat32 = 2;
constexpr uint16_t kHeaderV1Bytes = 28;
constexpr uint16_t kHeaderV2Bytes = 32;
constexpr uint32_t kExpectedRate = 48000;
constexpr uint16_t kMaxChannels = 2;

uint16_t ReadLE16(const uint8_t* data)
{
    uint16_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint32_t ReadLE32(const uint8_t* data)
{
    uint32_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint64_t ReadLE64(const uint8_t* data)
{
    uint64_t value = 0;
    memcpy(&value, data, sizeof(value));
    return value;
}

uint32_t BytesPerSample(AudioPacketProtocol::SampleFormat format)
{
    return format == AudioPacketProtocol::SampleFormat::Float32 ?
        sizeof(float) : sizeof(int16_t);
}
}

namespace AudioPacketProtocol
{
bool TryParse(const uint8_t* data, uint32_t bytes, Packet& packet)
{
    packet = {};
    if (!data || bytes < kHeaderV1Bytes || ReadLE32(data) != kMagic)
    {
        return false;
    }

    const uint16_t version = ReadLE16(data + 4);
    const uint16_t channels = ReadLE16(data + 6);
    const uint32_t rate = ReadLE32(data + 8);
    const uint32_t frames = ReadLE32(data + 12);
    const uint64_t sequence = ReadLE64(data + 16);
    const uint32_t payloadBytes = ReadLE32(data + 24);
    SampleFormat format = SampleFormat::Pcm16;
    uint16_t headerBytes = kHeaderV1Bytes;

    if (version == kVersionFloat32)
    {
        if (bytes < kHeaderV2Bytes) return false;
        format = static_cast<SampleFormat>(ReadLE16(data + 28));
        headerBytes = ReadLE16(data + 30);
        if (format != SampleFormat::Float32 || headerBytes != kHeaderV2Bytes)
        {
            return false;
        }
    }
    else if (version != kVersionPcm16)
    {
        return false;
    }

    const uint64_t expectedBytes =
        static_cast<uint64_t>(frames) * channels * BytesPerSample(format);
    if (rate != kExpectedRate || channels == 0 || channels > kMaxChannels ||
        frames == 0 || payloadBytes != bytes - headerBytes ||
        expectedBytes != payloadBytes)
    {
        return false;
    }

    packet.payload = data + headerBytes;
    packet.payloadBytes = payloadBytes;
    packet.channels = channels;
    packet.frames = frames;
    packet.sequence = sequence;
    packet.format = format;
    return true;
}
}
