#include "GameAudioProtocol.h"

#include <cstring>

namespace
{
constexpr uint32_t kMagic = 0x44533241;
constexpr uint16_t kVersion = 1;
constexpr uint16_t kHeaderBytes = 28;

template<typename T>
void Append(std::vector<uint8_t>& output, T value)
{
    const size_t offset = output.size();
    output.resize(offset + sizeof(value));
    std::memcpy(output.data() + offset, &value, sizeof(value));
}

template<typename T>
T Read(const uint8_t* bytes)
{
    T value{};
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}
}

namespace GameAudioProtocol
{
std::vector<uint8_t> EncodePcm16(
    std::span<const uint8_t> payload,
    uint64_t sequence)
{
    if (payload.size() != kPcm16PayloadBytes)
    {
        return {};
    }
    std::vector<uint8_t> packet;
    packet.reserve(kHeaderBytes + payload.size());
    Append(packet, kMagic);
    Append(packet, kVersion);
    Append(packet, kChannels);
    Append(packet, kSampleRate);
    Append(packet, kPacketFrames);
    Append(packet, sequence);
    Append(packet, static_cast<uint32_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

bool DecodePcm16(
    std::span<const uint8_t> bytes,
    PacketView& packet)
{
    packet = {};
    if (bytes.size() < kHeaderBytes ||
        Read<uint32_t>(bytes.data()) != kMagic ||
        Read<uint16_t>(bytes.data() + 4) != kVersion)
    {
        return false;
    }
    const uint16_t channels = Read<uint16_t>(bytes.data() + 6);
    const uint32_t sampleRate = Read<uint32_t>(bytes.data() + 8);
    const uint32_t frames = Read<uint32_t>(bytes.data() + 12);
    const uint64_t sequence = Read<uint64_t>(bytes.data() + 16);
    const uint32_t payloadBytes = Read<uint32_t>(bytes.data() + 24);
    const uint64_t expectedBytes =
        static_cast<uint64_t>(frames) * channels * sizeof(int16_t);
    if (channels != kChannels ||
        sampleRate != kSampleRate ||
        frames != kPacketFrames ||
        payloadBytes != bytes.size() - kHeaderBytes ||
        expectedBytes != payloadBytes)
    {
        return false;
    }
    packet.payload = bytes.data() + kHeaderBytes;
    packet.payloadBytes = payloadBytes;
    packet.channels = channels;
    packet.sampleRate = sampleRate;
    packet.frames = frames;
    packet.sequence = sequence;
    return true;
}
}
