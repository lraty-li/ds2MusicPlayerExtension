#pragma once

namespace RuntimeJacketBridge
{
constexpr unsigned int kMimeBytes = 96;
constexpr unsigned int kStageBytes = 96;
constexpr unsigned int kErrorBytes = 288;
constexpr unsigned int kSourceBytes = 384;
constexpr unsigned int kJacketSourceBytes = 80;

struct JacketInfo
{
    unsigned int version = 0;
    unsigned int bytes = 0;
    char mime[kMimeBytes] = {};
};

struct JacketStatus
{
    unsigned int version = 0;
    char stage[kStageBytes] = {};
    unsigned int bytes = 0;
    char mime[kMimeBytes] = {};
    char error[kErrorBytes] = {};
    char source[kSourceBytes] = {};
    char jacketSource[kJacketSourceBytes] = {};
};

bool EnsureReady();
bool ReadInfo(JacketInfo& info);
int ReadBytes(unsigned int knownVersion, void* out, unsigned int outBytes,
    unsigned int& written, char* mime, unsigned int mimeBytes);
bool ReadStatus(JacketStatus& status);
} // namespace RuntimeJacketBridge
