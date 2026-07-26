#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace GameAudioProtocol
{
inline constexpr uint32_t kSampleRate = 48000;
inline constexpr uint16_t kChannels = 2;
inline constexpr uint32_t kPacketFrames = 480;
inline constexpr size_t kPcm16PayloadBytes =
    kPacketFrames * kChannels * sizeof(int16_t);

struct PacketView
{
    const uint8_t* payload = nullptr;
    uint32_t payloadBytes = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint32_t frames = 0;
    uint64_t sequence = 0;
};

std::vector<uint8_t> EncodePcm16(
    std::span<const uint8_t> payload,
    uint64_t sequence);
bool DecodePcm16(
    std::span<const uint8_t> bytes,
    PacketView& packet);
}
