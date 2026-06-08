#pragma once

#include <cstdint>

namespace BrowserJacket
{
void UpdateFromJson(const char* json);
void UpdateStatusFromJson(const char* json);
int ReadInfo(uint32_t* version, uint32_t* bytes, char* mime, uint32_t mimeBytes);
int ReadBytes(uint32_t knownVersion, void* output, uint32_t outputBytes,
    uint32_t* bytesWritten, char* mime, uint32_t mimeBytes);
int ReadStatus(uint32_t* version, char* stage, uint32_t stageBytes,
    uint32_t* bytes, char* mime, uint32_t mimeBytes,
    char* error, uint32_t errorBytes, char* source, uint32_t sourceBytes,
    char* jacketSource, uint32_t jacketSourceBytes);
}
