#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct DecodedPcmChunk
{
    std::wstring streamId;
    uint64_t sequence = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t frames = 0;
    std::vector<uint8_t> bytes;
};

bool IsPcmChunkMessage(std::wstring_view message);
bool DecodePcmChunk(
    std::wstring_view message,
    DecodedPcmChunk& chunk,
    std::wstring& error);
