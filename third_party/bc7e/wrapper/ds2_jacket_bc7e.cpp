#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "bc7e_ispc.h"

namespace
{
constexpr uint32_t kBlockPixels = 16;
constexpr uint32_t kBatchBlocks = 64;

std::once_flag g_initOnce;

bool ValidateArgs(const uint8_t* rgba, uint32_t width, uint32_t height,
    const uint8_t* bc7, uint32_t bc7Bytes, uint64_t* requiredBytes)
{
    if (!rgba || !bc7 || width == 0 || height == 0 || !requiredBytes) return false;

    const uint64_t blocksX = (uint64_t(width) + 3) / 4;
    const uint64_t blocksY = (uint64_t(height) + 3) / 4;
    *requiredBytes = blocksX * blocksY * 16;
    return *requiredBytes <= bc7Bytes;
}

uint32_t LoadPixel(const uint8_t* rgba, uint32_t width, uint32_t x, uint32_t y)
{
    const uint8_t* p = rgba + (uint64_t(y) * width + x) * 4;
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
        (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

void FillBlockBatch(uint32_t* pixels, const uint8_t* rgba, uint32_t width,
    uint32_t height, uint32_t blockX, uint32_t blockY, uint32_t blockCount)
{
    for (uint32_t b = 0; b < blockCount; ++b)
    {
        const uint32_t baseX = (blockX + b) * 4;
        const uint32_t baseY = blockY * 4;
        uint32_t* out = pixels + uint64_t(b) * kBlockPixels;

        for (uint32_t py = 0; py < 4; ++py)
        {
            const uint32_t sy = std::min(baseY + py, height - 1);
            for (uint32_t px = 0; px < 4; ++px)
            {
                const uint32_t sx = std::min(baseX + px, width - 1);
                out[py * 4 + px] = LoadPixel(rgba, width, sx, sy);
            }
        }
    }
}
}

extern "C" __declspec(dllexport)
int __cdecl DS2_EncodeRgbaToBc7(const uint8_t* rgba, uint32_t width,
    uint32_t height, uint8_t* bc7, uint32_t bc7Bytes)
{
    uint64_t requiredBytes = 0;
    if (!ValidateArgs(rgba, width, height, bc7, bc7Bytes, &requiredBytes))
    {
        return 0;
    }

    std::call_once(g_initOnce, []() { ispc::bc7e_compress_block_init(); });

    ispc::bc7e_compress_block_params params = {};
    ispc::bc7e_compress_block_params_init_slowest(&params, false);

    const uint32_t blocksX = (width + 3) / 4;
    const uint32_t blocksY = (height + 3) / 4;
    uint32_t pixels[kBlockPixels * kBatchBlocks] = {};

    for (uint32_t by = 0; by < blocksY; ++by)
    {
        for (uint32_t bx = 0; bx < blocksX; bx += kBatchBlocks)
        {
            const uint32_t count = std::min(kBatchBlocks, blocksX - bx);
            FillBlockBatch(pixels, rgba, width, height, bx, by, count);
            uint8_t* row = bc7 + (uint64_t(by) * blocksX + bx) * 16;
            ispc::bc7e_compress_blocks(count, reinterpret_cast<uint64_t*>(row),
                pixels, &params);
        }
    }

    return requiredBytes <= bc7Bytes ? 1 : 0;
}

extern "C" __declspec(dllexport)
uint32_t __cdecl DS2_Bc7eWrapperVersion()
{
    return 1;
}
