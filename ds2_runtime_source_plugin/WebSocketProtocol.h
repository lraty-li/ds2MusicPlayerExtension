#pragma once

#include <winsock2.h>

#include <cstdint>

namespace WebSocketProtocol
{
bool Accept(SOCKET socket);
bool ReadBinaryFrame(SOCKET socket, uint8_t* payload, uint32_t maxBytes,
    uint32_t& payloadBytes);
}
