#include "pch.h"

#include "AudioSourceArbiter.h"

#include "AudioRingBuffer.h"
#include "PluginLog.h"
#include "WebSocketProtocol.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace
{
std::mutex g_mutex;
SOCKET g_listener = INVALID_SOCKET;
SOCKET g_active = INVALID_SOCKET;
std::vector<SOCKET> g_clients;
std::string g_activeId;
std::string g_activeKind;

std::string SafeText(const char* text)
{
    std::string value = text ? text : "";
    if (value.size() > 63) value.resize(63);
    for (char& character : value)
    {
        if (character < 0x20 || character == '"' || character == '\\')
        {
            character = '_';
        }
    }
    return value;
}

bool IsRegistered(SOCKET socket)
{
    return std::find(g_clients.begin(), g_clients.end(), socket) !=
        g_clients.end();
}

void LogClaim(const std::string& id, const std::string& kind,
    const std::string& reason, const std::string& previous, SOCKET socket)
{
    char line[384] = {};
    sprintf_s(line,
        "audio source active kind=\"%s\" id=\"%s\" reason=\"%s\" "
        "previous=\"%s\" socket=%llu",
        kind.c_str(), id.c_str(), reason.c_str(), previous.c_str(),
        static_cast<unsigned long long>(socket));
    PluginLog::Write(line);
}
}

namespace AudioSourceArbiter
{
void Reset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_listener = INVALID_SOCKET;
    g_active = INVALID_SOCKET;
    g_clients.clear();
    g_activeId.clear();
    g_activeKind.clear();
}

void SetListener(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_listener = socket;
}

void CloseListener(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_listener != socket) return;
    closesocket(g_listener);
    g_listener = INVALID_SOCKET;
}

void AddClient(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clients.push_back(socket);
}

void RemoveClient(SOCKET socket)
{
    bool released = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_clients.erase(
            std::remove(g_clients.begin(), g_clients.end(), socket),
            g_clients.end());
        if (g_active == socket)
        {
            g_active = INVALID_SOCKET;
            g_activeId.clear();
            g_activeKind.clear();
            released = true;
        }
    }
    if (released)
    {
        AudioRingBuffer::Clear();
        PluginLog::Write("audio source released; waiting for explicit claim");
    }
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_listener != INVALID_SOCKET)
    {
        closesocket(g_listener);
        g_listener = INVALID_SOCKET;
    }
    for (const SOCKET socket : g_clients)
    {
        shutdown(socket, SD_BOTH);
    }
    g_active = INVALID_SOCKET;
}

bool Claim(SOCKET socket, const char* sourceId,
    const char* sourceKind, const char* reason)
{
    const std::string id = SafeText(sourceId);
    const std::string kind = SafeText(sourceKind);
    const std::string why = SafeText(reason);
    if (id.empty() || kind.empty()) return false;

    SOCKET previousSocket = INVALID_SOCKET;
    std::string previous;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!IsRegistered(socket)) return false;
        changed = g_active != socket;
        if (changed)
        {
            previousSocket = g_active;
            if (!g_activeKind.empty())
            {
                previous = g_activeKind + "/" + g_activeId;
            }
            g_active = socket;
        }
        g_activeId = id;
        g_activeKind = kind;
        if (changed && previousSocket != INVALID_SOCKET)
        {
            WebSocketProtocol::SendTextFrame(
                previousSocket,
                "{\"type\":\"control\",\"command\":\"pause\","
                "\"reason\":\"source_preempted\"}");
        }
    }

    if (changed)
    {
        AudioRingBuffer::Clear();
        AudioRingBuffer::SnapshotStats(true);
        LogClaim(id, kind, why, previous, socket);
    }
    return true;
}

bool IsActive(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active == socket;
}

bool SendControl(const char* json)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_active != INVALID_SOCKET && json &&
        WebSocketProtocol::SendTextFrame(g_active, json);
}
}
