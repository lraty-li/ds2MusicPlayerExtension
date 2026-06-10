#pragma once

#include "Logger.h"

#include <cstdint>
#include <vector>

namespace JacketUploadPipeline
{
constexpr unsigned int kMaxJacketBytes = 2 * 1024 * 1024;

bool DecodeAndUpload(const std::vector<uint8_t>& buffer, unsigned int version,
    unsigned int written, const char* mime, const char* reason, const Logger& logger);
} // namespace JacketUploadPipeline
