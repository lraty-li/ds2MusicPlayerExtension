#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>
#include <vector>

struct AudioStreamClient
{
    SOCKET socket = INVALID_SOCKET;
    std::vector<uint8_t> payload;
    std::string sourceId;
    std::string sourceKind;
    bool protocolAware = false;
    uint64_t packets = 0;
    uint64_t frames = 0;
    uint64_t drops = 0;
    uint64_t lastSequence = UINT64_MAX;
    uint64_t lastLogTick = 0;
    uint64_t lastPacketTick = 0;
    uint32_t maxPacketGapMs = 0;
};

namespace AudioStreamClientIo
{
bool Accept(AudioStreamClient& client, SOCKET socket, uint32_t maxFrameBytes);
bool ReadAndProcess(AudioStreamClient& client, uint32_t maxFrameBytes);
void Close(AudioStreamClient& client);
}
