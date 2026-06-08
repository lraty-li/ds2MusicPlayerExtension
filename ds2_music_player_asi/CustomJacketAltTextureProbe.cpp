#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"

#include <sstream>

namespace
{
uint64_t g_altTarget = 0;
uint64_t g_altTexture = 0;
uint64_t g_altPixelBuffer = 0;
bool g_altPrepared = false;

bool ReadSlotPointer(uint64_t addr, CustomJacketSlot& slot)
{
    uint64_t slotAddr = 0;
    if (!CustomJacketInternal::SehReadU64(addr, slotAddr) || !slotAddr)
    {
        return false;
    }
    return CustomJacketInternal::SehReadSlot(slotAddr, slot);
}

bool ReadLoadedTexture(uint64_t target, uint64_t& loaded, uint64_t& texture)
{
    loaded = 0;
    texture = 0;
    if (!CustomJacketInternal::SehReadU64(target + 0x20, loaded) || !loaded)
    {
        return false;
    }
    return CustomJacketInternal::SehReadU64(loaded + 0x30, texture) && texture;
}
} // namespace

namespace CustomJacketInternal
{
void ResetAlternateJacketTextureProbe()
{
    g_altTarget = 0;
    g_altTexture = 0;
    g_altPixelBuffer = 0;
    g_altPrepared = false;
}

bool PrepareAlternateJacketTextureProbe(void* catalogueResource, const Logger& logger)
{
    if (!catalogueResource || g_altPrepared) return g_altTarget != 0;

    const uint64_t base = reinterpret_cast<uint64_t>(catalogueResource);
    CustomJacketSlot slot = {};
    if (!ReadSlotPointer(base + 0xC8, slot))
    {
        logger.Log("uiclone alt: DefaultConstructionHoloImageTexture slot unreadable");
        g_altPrepared = true;
        return false;
    }

    g_altTarget = slot.target;
    g_altPrepared = true;
    SehTriggerDetachedLoad(slot);

    std::ostringstream oss;
    oss << "uiclone alt prepared: source=DefaultConstructionHoloImageTexture"
        << " target=" << HookUtils::HexU64(g_altTarget);
    logger.Log(oss.str());
    return true;
}

bool TryCloneAlternateJacketTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger)
{
    newTarget = 0;
    if (!track || !g_altTarget) return false;

    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadLoadedTexture(g_altTarget, loaded, texture))
    {
        logger.Log("uiclone alt: override texture not loaded yet");
        return false;
    }
    g_altTexture = texture;

    std::ostringstream oss;
    oss << "uiclone alt loaded: loadedUI=" << HookUtils::HexU64(loaded)
        << " texture=" << HookUtils::HexU64(texture);
    logger.Log(oss.str());

    return CloneLoadedUiTextureWithTextureOverrideToTrack(track,
        g_altTexture, "DefaultConstructionHoloImageTexture", newTarget, logger);
}

bool TryGetAlternateJacketPixelBuffer(uint64_t& pixelBuffer, const Logger& logger)
{
    pixelBuffer = 0;
    if (!g_altTarget) return false;

    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadLoadedTexture(g_altTarget, loaded, texture))
    {
        logger.Log("uiclone altpb cmp: override texture not loaded yet");
        return false;
    }
    g_altTexture = texture;
    if (!SehReadU64(texture + 0x20, g_altPixelBuffer) || !g_altPixelBuffer)
    {
        logger.Log("uiclone altpb cmp: override Texture has no pixelBuffer");
        return false;
    }

    pixelBuffer = g_altPixelBuffer;
    return true;
}

bool TryCloneAlternatePixelBufferToTrack(void* track, uint64_t& newTarget, const Logger& logger)
{
    newTarget = 0;
    if (!track || !g_altTarget) return false;

    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadLoadedTexture(g_altTarget, loaded, texture))
    {
        logger.Log("uiclone altpb: override texture not loaded yet");
        return false;
    }
    g_altTexture = texture;
    if (!SehReadU64(texture + 0x20, g_altPixelBuffer) || !g_altPixelBuffer)
    {
        logger.Log("uiclone altpb: override Texture has no pixelBuffer");
        return false;
    }

    std::ostringstream oss;
    oss << "uiclone altpb loaded: loadedUI=" << HookUtils::HexU64(loaded)
        << " texture=" << HookUtils::HexU64(texture)
        << " pixelBuffer=" << HookUtils::HexU64(g_altPixelBuffer);
    logger.Log(oss.str());

    return CloneLoadedUiTextureWithPixelBufferOverrideToTrack(track,
        g_altPixelBuffer, "DefaultConstructionHoloImageTexture", newTarget, logger);
}
} // namespace CustomJacketInternal
