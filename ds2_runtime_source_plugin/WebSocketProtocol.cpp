#include "pch.h"

#include "WebSocketProtocol.h"

#include <wincrypt.h>

#include <cstdio>
#include <cstring>

namespace
{
bool ShouldStop()
{
    return false;
}

bool RecvAll(SOCKET socket, void* buffer, int bytes)
{
    auto* cursor = static_cast<char*>(buffer);
    int total = 0;
    while (total < bytes && !ShouldStop())
    {
        const int got = recv(socket, cursor + total, bytes - total, 0);
        if (got <= 0) return false;
        total += got;
    }
    return total == bytes;
}

bool SendAll(SOCKET socket, const char* data, int bytes)
{
    int total = 0;
    while (total < bytes && !ShouldStop())
    {
        const int sent = send(socket, data + total, bytes - total, 0);
        if (sent <= 0) return false;
        total += sent;
    }
    return total == bytes;
}

char* Trim(char* text)
{
    while (*text == ' ' || *text == '\t') ++text;
    char* end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
        end[-1] == '\r' || end[-1] == '\n'))
    {
        *--end = 0;
    }
    return text;
}

bool Base64Sha1(const char* text, char* out, DWORD outBytes)
{
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[20] = {};
    DWORD digestBytes = sizeof(digest);
    const DWORD flags = CRYPT_VERIFYCONTEXT | CRYPT_SILENT;
    if (!CryptAcquireContextA(&prov, nullptr, nullptr, PROV_RSA_FULL, flags)) return false;
    if (!CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash))
    {
        CryptReleaseContext(prov, 0);
        return false;
    }
    CryptHashData(hash, reinterpret_cast<const BYTE*>(text),
        static_cast<DWORD>(strlen(text)), 0);
    const BOOL ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &digestBytes, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    if (!ok) return false;
    return CryptBinaryToStringA(digest, digestBytes,
        CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &outBytes) != FALSE;
}

uint64_t ReadBigEndian(const uint8_t* data, int bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < bytes; ++i) value = (value << 8) | data[i];
    return value;
}
}

namespace WebSocketProtocol
{
bool Accept(SOCKET socket)
{
    char request[8192] = {};
    int used = 0;
    while (used < static_cast<int>(sizeof(request) - 1))
    {
        if (!RecvAll(socket, request + used, 1)) return false;
        ++used;
        if (used >= 4 && memcmp(request + used - 4, "\r\n\r\n", 4) == 0) break;
    }

    char* key = nullptr;
    char* context = nullptr;
    for (char* line = strtok_s(request, "\n", &context);
        line; line = strtok_s(nullptr, "\n", &context))
    {
        if (_strnicmp(line, "Sec-WebSocket-Key:", 18) == 0)
        {
            key = Trim(line + 18);
            break;
        }
    }
    if (!key || !*key) return false;

    char seed[160] = {};
    sprintf_s(seed, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    char accept[64] = {};
    if (!Base64Sha1(seed, accept, sizeof(accept))) return false;

    char response[256] = {};
    sprintf_s(response,
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept);
    return SendAll(socket, response, static_cast<int>(strlen(response)));
}

bool ReadFrame(SOCKET socket, uint8_t* payload, uint32_t maxBytes,
    uint32_t& payloadBytes, uint8_t& opcode)
{
    payloadBytes = 0;
    opcode = 0;
    uint8_t header[2] = {};
    if (!RecvAll(socket, header, sizeof(header))) return false;
    opcode = header[0] & 0x0F;
    const bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;
    if (opcode == 0x8) return false;
    if (length == 126)
    {
        uint8_t ext[2] = {};
        if (!RecvAll(socket, ext, sizeof(ext))) return false;
        length = ReadBigEndian(ext, 2);
    }
    else if (length == 127)
    {
        uint8_t ext[8] = {};
        if (!RecvAll(socket, ext, sizeof(ext))) return false;
        length = ReadBigEndian(ext, 8);
    }
    if (!masked || length > maxBytes) return false;

    uint8_t mask[4] = {};
    if (!RecvAll(socket, mask, sizeof(mask))) return false;
    if (!RecvAll(socket, payload, static_cast<int>(length))) return false;
    for (uint64_t i = 0; i < length; ++i) payload[i] ^= mask[i & 3];
    payloadBytes = static_cast<uint32_t>(length);
    return true;
}

bool ReadBinaryFrame(SOCKET socket, uint8_t* payload, uint32_t maxBytes,
    uint32_t& payloadBytes)
{
    uint8_t opcode = 0;
    if (!ReadFrame(socket, payload, maxBytes, payloadBytes, opcode)) return false;
    if (opcode != 0x2) payloadBytes = 0;
    return true;
}

bool SendTextFrame(SOCKET socket, const char* text)
{
    if (!text) return false;
    const size_t length = strlen(text);
    if (length > 65535) return false;

    uint8_t header[4] = {};
    int headerBytes = 2;
    header[0] = 0x81;
    if (length < 126)
    {
        header[1] = static_cast<uint8_t>(length);
    }
    else
    {
        header[1] = 126;
        header[2] = static_cast<uint8_t>((length >> 8) & 0xFF);
        header[3] = static_cast<uint8_t>(length & 0xFF);
        headerBytes = 4;
    }

    return SendAll(socket, reinterpret_cast<const char*>(header), headerBytes) &&
        SendAll(socket, text, static_cast<int>(length));
}
}
