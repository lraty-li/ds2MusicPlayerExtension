#include "WebSocketWire.h"

#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <ws2tcpip.h>

#include <array>
#include <string>

namespace
{
constexpr char kWebSocketGuid[] =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

bool SendAll(SOCKET socket, const void* data, size_t bytes)
{
    const auto* cursor = static_cast<const char*>(data);
    size_t sentBytes = 0;
    while (sentBytes < bytes)
    {
        const int sent = send(
            socket,
            cursor + sentBytes,
            static_cast<int>(bytes - sentBytes),
            0);
        if (sent <= 0) return false;
        sentBytes += static_cast<size_t>(sent);
    }
    return true;
}

bool ReceiveAll(SOCKET socket, void* data, size_t bytes)
{
    auto* cursor = static_cast<char*>(data);
    size_t receivedBytes = 0;
    while (receivedBytes < bytes)
    {
        const int received = recv(
            socket,
            cursor + receivedBytes,
            static_cast<int>(bytes - receivedBytes),
            0);
        if (received <= 0) return false;
        receivedBytes += static_cast<size_t>(received);
    }
    return true;
}

bool ReceiveHttpHeader(SOCKET socket, std::string& header)
{
    header.clear();
    while (header.size() < 8192)
    {
        char character = 0;
        if (!ReceiveAll(socket, &character, 1)) return false;
        header.push_back(character);
        if (header.ends_with("\r\n\r\n")) return true;
    }
    return false;
}

std::string Sha1Base64(std::string_view text)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    std::array<BYTE, 20> digest{};
    DWORD digestBytes = static_cast<DWORD>(digest.size());
    if (!CryptAcquireContextA(
            &provider,
            nullptr,
            nullptr,
            PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT) ||
        !CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash))
    {
        if (provider) CryptReleaseContext(provider, 0);
        return {};
    }
    const BOOL hashed = CryptHashData(
        hash,
        reinterpret_cast<const BYTE*>(text.data()),
        static_cast<DWORD>(text.size()),
        0);
    const BOOL read = hashed && CryptGetHashParam(
        hash, HP_HASHVAL, digest.data(), &digestBytes, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!read) return {};

    std::array<char, 64> encoded{};
    DWORD encodedBytes = static_cast<DWORD>(encoded.size());
    if (!CryptBinaryToStringA(
            digest.data(),
            digestBytes,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            encoded.data(),
            &encodedBytes))
    {
        return {};
    }
    return encoded.data();
}

std::string RandomKey()
{
    std::array<BYTE, 16> random{};
    if (BCryptGenRandom(
            nullptr,
            random.data(),
            static_cast<ULONG>(random.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    {
        return {};
    }
    std::array<char, 64> encoded{};
    DWORD encodedBytes = static_cast<DWORD>(encoded.size());
    if (!CryptBinaryToStringA(
            random.data(),
            static_cast<DWORD>(random.size()),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            encoded.data(),
            &encodedBytes))
    {
        return {};
    }
    return encoded.data();
}
}

namespace WebSocketWire
{
SOCKET ConnectLocal(uint16_t port, std::string& error)
{
    error.clear();
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET)
    {
        error = "socket failed";
        return INVALID_SOCKET;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    InetPtonA(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(
            socket,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0)
    {
        error = "connect failed: " + std::to_string(WSAGetLastError());
        Close(socket);
        return INVALID_SOCKET;
    }

    const std::string key = RandomKey();
    const std::string expected = Sha1Base64(key + kWebSocketGuid);
    const std::string request =
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:" +
        std::to_string(port) +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: " +
        key + "\r\n\r\n";
    std::string response;
    if (key.empty() || expected.empty() ||
        !SendAll(socket, request.data(), request.size()) ||
        !ReceiveHttpHeader(socket, response) ||
        response.find(" 101 ") == std::string::npos ||
        response.find(expected) == std::string::npos)
    {
        error = "websocket handshake failed";
        Close(socket);
        return INVALID_SOCKET;
    }
    return socket;
}

bool AcceptServer(SOCKET socket, std::string& error)
{
    std::string request;
    if (!ReceiveHttpHeader(socket, request))
    {
        error = "request header failed";
        return false;
    }
    constexpr std::string_view prefix = "Sec-WebSocket-Key:";
    const size_t keyAt = request.find(prefix);
    if (keyAt == std::string::npos)
    {
        error = "missing websocket key";
        return false;
    }
    const size_t valueAt = request.find_first_not_of(
        " \t", keyAt + prefix.size());
    const size_t valueEnd = request.find("\r\n", valueAt);
    const std::string key = request.substr(valueAt, valueEnd - valueAt);
    const std::string accept = Sha1Base64(key + kWebSocketGuid);
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    if (accept.empty() ||
        !SendAll(socket, response.data(), response.size()))
    {
        error = "response handshake failed";
        return false;
    }
    return true;
}
}
