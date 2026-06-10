#include "pch.h"

#include "CustomJacketImageLayout.h"

#include <algorithm>

namespace
{
uint32_t ScaleCrop(uint32_t value, uint32_t source, uint32_t target)
{
    const uint64_t scaled = (uint64_t(value) * target + source / 2) / source;
    return std::max<uint32_t>(1, static_cast<uint32_t>(scaled));
}

CustomJacketImageLayout::Layout ComputeCover(uint32_t sourceW, uint32_t sourceH,
    uint32_t targetW, uint32_t targetH)
{
    CustomJacketImageLayout::Layout layout;
    layout.targetW = targetW;
    layout.targetH = targetH;
    layout.drawW = targetW;
    layout.drawH = targetH;
    if (!sourceW || !sourceH || !targetW || !targetH)
    {
        return layout;
    }

    uint32_t cropW = sourceW;
    uint32_t cropH = sourceH;
    if (uint64_t(sourceW) * targetH > uint64_t(sourceH) * targetW)
    {
        cropW = ScaleCrop(sourceH, targetH, targetW);
    }
    else if (uint64_t(sourceW) * targetH < uint64_t(sourceH) * targetW)
    {
        cropH = ScaleCrop(sourceW, targetW, targetH);
    }
    cropW = cropW < sourceW ? cropW : sourceW;
    cropH = cropH < sourceH ? cropH : sourceH;

    layout.source.x = (sourceW - cropW) / 2;
    layout.source.y = (sourceH - cropH) / 2;
    layout.source.width = cropW;
    layout.source.height = cropH;
    return layout;
}
} // namespace

namespace CustomJacketImageLayout
{
Layout Compute(Mode mode, uint32_t sourceW, uint32_t sourceH,
    uint32_t targetW, uint32_t targetH)
{
    switch (mode)
    {
    case Mode::Cover:
    default:
        return ComputeCover(sourceW, sourceH, targetW, targetH);
    }
}
} // namespace CustomJacketImageLayout
