#include "pch.h"

#include "CustomJacketInternal.h"

#include "GameLayout.h"
#include "HookUtils.h"

#include <sstream>

namespace
{
uint64_t g_sourceTarget = 0;
uint64_t g_sourceTexture = 0;
uint64_t g_sourceTextureDx12 = 0;
bool g_sourcePrepared = false;
bool g_sourceLogged = false;

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
    if (!CustomJacketInternal::SehReadU64(
        target + GameLayout::StreamingTarget::kLoaded, loaded) || !loaded)
    {
        return false;
    }
    return CustomJacketInternal::SehReadU64(
        loaded + GameLayout::UiTexture::kTexture, texture) && texture;
}

bool ReadTextureDx12Resource(uint64_t textureDx12, uint64_t& wrapper, uint64_t& resource)
{
    wrapper = 0;
    resource = 0;
    return textureDx12
        && CustomJacketInternal::SehReadU64(
            textureDx12 + GameLayout::TextureDx12::kMainWrapper, wrapper)
        && wrapper
        && CustomJacketInternal::SehReadU64(
            wrapper + GameLayout::ResourceWrapper::kD3D12Resource, resource)
        && resource;
}
} // namespace

namespace CustomJacketInternal
{
void ResetSourceJacketTexture()
{
    g_sourceTarget = 0;
    g_sourceTexture = 0;
    g_sourceTextureDx12 = 0;
    g_sourcePrepared = false;
    g_sourceLogged = false;
}

bool PrepareSourceJacketTexture(void* catalogueResource, const Logger& logger)
{
    if (!catalogueResource || g_sourcePrepared) return g_sourceTarget != 0;

    const uint64_t base = reinterpret_cast<uint64_t>(catalogueResource);
    CustomJacketSlot slot = {};
    if (!ReadSlotPointer(
        base + GameLayout::CatalogueResource::kDefaultConstructionHoloImageTexture,
        slot))
    {
        logger.Log("jacket source texture: DefaultConstructionHoloImageTexture slot unreadable");
        g_sourcePrepared = true;
        return false;
    }

    g_sourceTarget = slot.target;
    g_sourcePrepared = true;
    SehTriggerDetachedLoad(slot);

    std::ostringstream oss;
    oss << "jacket source texture prepared: source=DefaultConstructionHoloImageTexture"
        << " target=" << HookUtils::HexU64(g_sourceTarget);
    logger.Log(oss.str());
    return true;
}

bool TryGetSourceJacketTextureDx12(uint64_t& textureDx12, const Logger& logger)
{
    textureDx12 = 0;
    if (!g_sourceTarget) return false;

    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadLoadedTexture(g_sourceTarget, loaded, texture))
    {
        return false;
    }
    g_sourceTexture = texture;
    if (!SehReadU64(texture + GameLayout::Texture::kTextureDx12,
        g_sourceTextureDx12) || !g_sourceTextureDx12)
    {
        return false;
    }
    uint64_t wrapper = 0;
    uint64_t resource = 0;
    if (!ReadTextureDx12Resource(g_sourceTextureDx12, wrapper, resource))
    {
        return false;
    }

    if (!g_sourceLogged)
    {
        std::ostringstream oss;
        oss << "jacket source texture loaded: loadedUI=" << HookUtils::HexU64(loaded)
            << " texture=" << HookUtils::HexU64(g_sourceTexture)
            << " textureDx12=" << HookUtils::HexU64(g_sourceTextureDx12)
            << " wrapper=" << HookUtils::HexU64(wrapper)
            << " resource=" << HookUtils::HexU64(resource);
        logger.Log(oss.str());
        g_sourceLogged = true;
    }
    textureDx12 = g_sourceTextureDx12;
    return true;
}
} // namespace CustomJacketInternal
