#pragma once

#include <cstdint>

namespace CustomJacketImageLayout
{
constexpr uint32_t kDefaultTargetWidth = 640;
constexpr uint32_t kDefaultTargetHeight = 640;

enum class Mode
{
    Cover,
};

struct Rect
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Layout
{
    Rect source;
    uint32_t drawW = 0;
    uint32_t drawH = 0;
    uint32_t targetW = 0;
    uint32_t targetH = 0;
};

Layout Compute(Mode mode, uint32_t sourceW, uint32_t sourceH,
    uint32_t targetW, uint32_t targetH);
} // namespace CustomJacketImageLayout
