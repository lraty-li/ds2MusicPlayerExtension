#pragma once

#include <winsock2.h>

namespace AudioSourceArbiter
{
void Reset();
void SetListener(SOCKET socket);
void CloseListener(SOCKET socket);
void AddClient(SOCKET socket);
void RemoveClient(SOCKET socket);
void Shutdown();

bool Claim(SOCKET socket, const char* sourceId,
    const char* sourceKind, const char* reason);
bool IsActive(SOCKET socket);
bool SendControl(const char* json);
}
