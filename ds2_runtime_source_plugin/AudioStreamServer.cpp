#include "pch.h"

#include "AudioStreamServer.h"

#include "AudioRingBuffer.h"
#include "AudioSourceArbiter.h"
#include "AudioStreamClient.h"
#include "BrowserMetadata.h"
#include "PluginLog.h"

#include <ws2tcpip.h>

#include <cstdlib>
#include <cstdint>
#include <vector>

namespace
{
constexpr uint16_t kPort = 47832;
constexpr uint32_t kMaxFrameBytes = 4 * 1024 * 1024;
constexpr size_t kMaxClients = 8;

HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;

uint16_t ResolvePort()
{
    char text[16] = {};
    const DWORD length = GetEnvironmentVariableA(
        "DS2_AUDIO_STREAM_PORT", text, sizeof(text));
    if (!length || length >= sizeof(text)) return kPort;
    char* end = nullptr;
    const unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end || !value || value > 65535) return kPort;
    return static_cast<uint16_t>(value);
}

bool ShouldStop()
{
    return g_stopEvent &&
        WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0;
}

bool BindListen(SOCKET listener, uint16_t port)
{
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    return bind(listener, reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == 0 && listen(listener, SOMAXCONN) == 0;
}

void AcceptClient(SOCKET listener, std::vector<AudioStreamClient>& clients)
{
    SOCKET socket = accept(listener, nullptr, nullptr);
    if (socket == INVALID_SOCKET) return;
    if (clients.size() >= kMaxClients)
    {
        closesocket(socket);
        PluginLog::Write("audio websocket rejected: client limit");
        return;
    }
    AudioStreamClient client;
    if (AudioStreamClientIo::Accept(client, socket, kMaxFrameBytes))
    {
        clients.push_back(std::move(client));
    }
}

void ProcessClients(fd_set& readable,
    std::vector<AudioStreamClient>& clients)
{
    for (size_t index = clients.size(); index-- > 0;)
    {
        AudioStreamClient& client = clients[index];
        if (!FD_ISSET(client.socket, &readable)) continue;
        if (AudioStreamClientIo::ReadAndProcess(client, kMaxFrameBytes))
        {
            continue;
        }
        AudioStreamClientIo::Close(client);
        clients.erase(clients.begin() + index);
    }
}

void CloseClients(std::vector<AudioStreamClient>& clients)
{
    for (AudioStreamClient& client : clients)
    {
        AudioStreamClientIo::Close(client);
    }
    clients.clear();
}

void Serve(SOCKET listener)
{
    std::vector<AudioStreamClient> clients;
    while (!ShouldStop())
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        for (const AudioStreamClient& client : clients)
        {
            FD_SET(client.socket, &readable);
        }
        timeval timeout = {};
        timeout.tv_usec = 200000;
        const int selected = select(0, &readable, nullptr, nullptr, &timeout);
        if (selected == SOCKET_ERROR)
        {
            if (ShouldStop()) break;
            PluginLog::Write("audio websocket select failed");
            break;
        }
        if (!selected) continue;
        if (FD_ISSET(listener, &readable))
        {
            AcceptClient(listener, clients);
        }
        ProcessClients(readable, clients);
    }
    CloseClients(clients);
}

DWORD WINAPI ServerThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    WSADATA winsock = {};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 0;
    AudioSourceArbiter::Reset();
    PluginLog::Write("audio websocket server thread started");
    const uint16_t port = ResolvePort();

    while (!ShouldStop())
    {
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) break;
        AudioSourceArbiter::SetListener(listener);
        if (!BindListen(listener, port))
        {
            PluginLog::Write("audio websocket listen failed");
            AudioSourceArbiter::CloseListener(listener);
            Sleep(1000);
            continue;
        }
        char line[128] = {};
        sprintf_s(line,
            "audio websocket listening on 127.0.0.1:%u multi-source",
            port);
        PluginLog::Write(line);
        Serve(listener);
        AudioSourceArbiter::CloseListener(listener);
        if (!ShouldStop()) Sleep(250);
    }
    PluginLog::Write("audio websocket server thread stopped");
    WSACleanup();
    return 0;
}
}

namespace AudioStreamServer
{
void Start()
{
    if (g_thread) return;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
}

void Stop()
{
    if (g_stopEvent) SetEvent(g_stopEvent);
    AudioSourceArbiter::Shutdown();
    if (g_thread)
    {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_stopEvent)
    {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
}

uint32_t Read(float* const* outputs, uint32_t frames, uint32_t channels)
{
    return AudioRingBuffer::Read(outputs, frames, channels);
}

bool SendControl(const char* json)
{
    return AudioSourceArbiter::SendControl(json);
}

int ReadMetadataTitle(char* output, uint32_t outputBytes)
{
    return BrowserMetadata::ReadTitle(output, outputBytes);
}

int ReadMetadata(char* title, uint32_t titleBytes,
    char* artist, uint32_t artistBytes)
{
    return BrowserMetadata::Read(
        title, titleBytes, artist, artistBytes);
}

int ReadPlaybackState(uint32_t* version, int* known, int* paused)
{
    return BrowserMetadata::ReadPlaybackState(version, known, paused);
}
}
