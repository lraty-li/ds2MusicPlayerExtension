#pragma once

#include <winsock2.h>

#include <cstdint>

namespace WebSocketProtocol
{
bool Accept(SOCKET socket);
bool ReadFrame(SOCKET socket, uint8_t* payload, uint32_t maxBytes,
    uint32_t& payloadBytes, uint8_t& opcode);
bool ReadBinaryFrame(SOCKET socket, uint8_t* payload, uint32_t maxBytes,
    uint32_t& payloadBytes);
bool SendTextFrame(SOCKET socket, const char* text);
}
