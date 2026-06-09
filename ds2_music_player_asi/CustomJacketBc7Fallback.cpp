#include "pch.h"

#include "CustomJacketInternal.h"

#include <algorithm>

namespace
{
struct Color
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

void PutBits(uint8_t* block, uint32_t& bitPos, uint64_t value, uint32_t bits)
{
    for (uint32_t i = 0; i < bits; ++i, ++bitPos)
    {
        if ((value >> i) & 1) block[bitPos / 8] |= uint8_t(1u << (bitPos % 8));
    }
}

int ColorDistSq(const Color& a, const Color& b)
{
    const int dr = int(a.r) - int(b.r);
    const int dg = int(a.g) - int(b.g);
    const int db = int(a.b) - int(b.b);
    return dr * dr + dg * dg + db * db;
}

Color EndpointColor(const Color& color)
{
    return Color{ uint8_t(color.r | 1), uint8_t(color.g | 1), uint8_t(color.b | 1) };
}

uint32_t MinU32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

Color LerpEndpoint(const Color& a, const Color& b, int weight)
{
    return Color{
        uint8_t((int(a.r) * (64 - weight) + int(b.r) * weight + 32) >> 6),
        uint8_t((int(a.g) * (64 - weight) + int(b.g) * weight + 32) >> 6),
        uint8_t((int(a.b) * (64 - weight) + int(b.b) * weight + 32) >> 6)
    };
}

uint8_t BestIndex(const Color& color, const Color& a, const Color& b)
{
    static constexpr int kWeights[16] =
        { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    int bestError = INT_MAX;
    uint8_t bestIndex = 0;
    for (uint8_t i = 0; i < 16; ++i)
    {
        const Color decoded = LerpEndpoint(a, b, kWeights[i]);
        const int error = ColorDistSq(color, decoded);
        if (error < bestError)
        {
            bestError = error;
            bestIndex = i;
        }
    }
    return bestIndex;
}

void FillIndices(uint8_t* indices, const Color* colors, const Color& a, const Color& b)
{
    for (uint32_t i = 0; i < 16; ++i) indices[i] = BestIndex(colors[i], a, b);
}

void EncodeBc7Mode6(uint8_t* block, Color endpoint0, Color endpoint1,
    const uint8_t* indices)
{
    memset(block, 0, 16);
    endpoint0 = EndpointColor(endpoint0);
    endpoint1 = EndpointColor(endpoint1);
    uint32_t bitPos = 0;
    PutBits(block, bitPos, 0x40, 7);
    PutBits(block, bitPos, endpoint0.r >> 1, 7);
    PutBits(block, bitPos, endpoint1.r >> 1, 7);
    PutBits(block, bitPos, endpoint0.g >> 1, 7);
    PutBits(block, bitPos, endpoint1.g >> 1, 7);
    PutBits(block, bitPos, endpoint0.b >> 1, 7);
    PutBits(block, bitPos, endpoint1.b >> 1, 7);
    PutBits(block, bitPos, 0x7F, 7);
    PutBits(block, bitPos, 0x7F, 7);
    PutBits(block, bitPos, 1, 1);
    PutBits(block, bitPos, 1, 1);
    PutBits(block, bitPos, indices[0] & 7, 3);
    for (uint32_t i = 1; i < 16; ++i) PutBits(block, bitPos, indices[i], 4);
}

void EncodeBc7Block(uint8_t* block, const Color* colors)
{
    uint32_t first = 0;
    uint32_t second = 0;
    int bestDistance = -1;
    for (uint32_t a = 0; a < 16; ++a)
    {
        for (uint32_t b = a + 1; b < 16; ++b)
        {
            const int distance = ColorDistSq(colors[a], colors[b]);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                first = a;
                second = b;
            }
        }
    }

    if (bestDistance <= 4)
    {
        uint32_t r = 0, g = 0, b = 0;
        for (uint32_t i = 0; i < 16; ++i)
        {
            r += colors[i].r; g += colors[i].g; b += colors[i].b;
        }
        const Color avg{ uint8_t(r / 16), uint8_t(g / 16), uint8_t(b / 16) };
        const uint8_t indices[16] = {};
        EncodeBc7Mode6(block, avg, avg, indices);
        return;
    }

    Color endpoint0 = colors[first];
    Color endpoint1 = colors[second];
    uint8_t indices[16] = {};
    FillIndices(indices, colors, EndpointColor(endpoint0), EndpointColor(endpoint1));
    if (indices[0] > 7)
    {
        std::swap(endpoint0, endpoint1);
        FillIndices(indices, colors, EndpointColor(endpoint0), EndpointColor(endpoint1));
    }
    EncodeBc7Mode6(block, endpoint0, endpoint1, indices);
}
}

namespace CustomJacketInternal
{
void FillFallbackBc7FromRgba(uint8_t* dst, uint64_t dstOffset,
    uint32_t dstW, uint32_t dstH, uint32_t rowPitch,
    const uint8_t* rgba, uint32_t srcW, uint32_t srcH)
{
    const uint32_t bw = (dstW + 3) / 4;
    const uint32_t bh = (dstH + 3) / 4;
    for (uint32_t by = 0; by < bh; ++by)
    {
        auto* row = dst + dstOffset + uint64_t(by) * rowPitch;
        for (uint32_t bx = 0; bx < bw; ++bx)
        {
            Color colors[16] = {};
            for (uint32_t yy = 0; yy < 4; ++yy)
            {
                const uint32_t dstY = MinU32(by * 4 + yy, dstH - 1);
                const uint32_t y = uint32_t((uint64_t(dstY) * srcH) / dstH);
                for (uint32_t xx = 0; xx < 4; ++xx)
                {
                    const uint32_t dstX = MinU32(bx * 4 + xx, dstW - 1);
                    const uint32_t x = uint32_t((uint64_t(dstX) * srcW) / dstW);
                    const uint8_t* p = rgba + (uint64_t(y) * srcW + x) * 4;
                    colors[yy * 4 + xx] = Color{ p[0], p[1], p[2] };
                }
            }
            EncodeBc7Block(row + bx * 16, colors);
        }
    }
}
}
