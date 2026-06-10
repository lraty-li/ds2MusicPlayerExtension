#include "pch.h"

#include "JacketPendingImage.h"

#include <mutex>

namespace
{
std::mutex g_mutex;
JacketPendingImage::Image g_image;
} // namespace

namespace JacketPendingImage
{
void Store(const std::vector<uint8_t>& bytes, unsigned int version, const char* mime)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_image.bytes = bytes;
    g_image.mime = mime ? mime : "";
    g_image.version = version;
}

bool Snapshot(Image& image)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_image.bytes.empty()) return false;
    image = g_image;
    return true;
}
} // namespace JacketPendingImage
