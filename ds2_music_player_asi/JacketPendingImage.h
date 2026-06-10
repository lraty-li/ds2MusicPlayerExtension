#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace JacketPendingImage
{
struct Image
{
    std::vector<uint8_t> bytes;
    std::string mime;
    unsigned int version = 0;
};

void Store(const std::vector<uint8_t>& bytes, unsigned int version, const char* mime);
bool Snapshot(Image& image);
} // namespace JacketPendingImage
