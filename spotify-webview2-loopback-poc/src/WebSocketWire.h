#pragma once

#include <winsock2.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace WebSocketWire
{
enum class ReadStatus
{
    NoData,
    Message,
    Closed,
    Error,
};

struct Message
{
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
};

SOCKET ConnectLocal(uint16_t port, std::string& error);
bool AcceptServer(SOCKET socket, std::string& error);
bool SendClientBinary(
    SOCKET socket,
    std::span<const uint8_t> payload);
bool SendClientText(SOCKET socket, std::string_view text);
bool SendServerText(SOCKET socket, std::string_view text);
ReadStatus TryRead(
    SOCKET socket,
    bool peerFramesAreMasked,
    uint32_t timeoutMs,
    Message& message);
void Close(SOCKET& socket);
}
