#include "pch.h"

#include "CustomJacketInternal.h"

#include "GameLayout.h"
#include "HookUtils.h"
#include "SpecialTrackHelpers.h"

#include <sstream>

namespace
{
bool ReadCurrentLoaded(void* track, uint64_t& slotAddr,
    CustomJacketSlot& slot, uint64_t& loaded, uint64_t& texture)
{
    auto* bytes = static_cast<uint8_t*>(track);
    if (!CustomJacketInternal::SehReadU64(
        reinterpret_cast<uint64_t>(bytes + GameLayout::Track::kJacket), slotAddr)
        || !slotAddr
        || !CustomJacketInternal::SehReadSlot(slotAddr, slot)
        || !CustomJacketInternal::SehReadU64(
            slot.target + GameLayout::StreamingTarget::kLoaded, loaded)
        || !loaded)
    {
        return false;
    }
    return CustomJacketInternal::SehReadU64(
        loaded + GameLayout::UiTexture::kTexture, texture) && texture;
}

bool ReadTextureDx12(uint64_t texture, uint64_t& textureDx12)
{
    __try
    {
        textureDx12 = *reinterpret_cast<uint64_t*>(
            texture + GameLayout::Texture::kTextureDx12);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        textureDx12 = 0;
        return false;
    }
}

uint8_t* CloneTexture(uint64_t sourceTexture, const Logger& logger)
{
    uint64_t textureDx12 = 0;
    if (!ReadTextureDx12(sourceTexture, textureDx12) || !textureDx12)
    {
        logger.Log("uiclone: source Texture has no TextureDX12");
        return nullptr;
    }

    uint64_t cloneSize = 0;
    uint32_t relocated = 0;
    uint32_t relocatedExt38 = 0;
    auto* textureDx12Copy = CustomJacketInternal::ClonePixelBufferForUiClone(textureDx12,
        cloneSize, relocated, relocatedExt38, logger);
    if (!textureDx12Copy) return nullptr;

    auto* texture = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x600));
    if (!texture)
    {
        logger.Log("uiclone: alloc Texture failed");
        VirtualFree(textureDx12Copy, 0, MEM_RELEASE);
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(
        texture, reinterpret_cast<void*>(sourceTexture), 0x70))
    {
        logger.Log("uiclone: memcpy Texture header failed");
        VirtualFree(textureDx12Copy, 0, MEM_RELEASE);
        return nullptr;
    }

    *reinterpret_cast<uint64_t*>(texture + GameLayout::Texture::kTextureDx12) =
        reinterpret_cast<uint64_t>(textureDx12Copy);
    *reinterpret_cast<uint64_t*>(texture + GameLayout::Texture::kChain0) =
        reinterpret_cast<uint64_t>(texture + GameLayout::Texture::kChain1);
    *reinterpret_cast<uint64_t*>(texture + GameLayout::Texture::kChain1) =
        reinterpret_cast<uint64_t>(texture + GameLayout::Texture::kChain2);
    *reinterpret_cast<uint64_t*>(texture + GameLayout::Texture::kChain2) =
        reinterpret_cast<uint64_t>(texture + GameLayout::Texture::kChain3);
    *reinterpret_cast<uint64_t*>(texture + GameLayout::Texture::kChain3) = 0;
    *reinterpret_cast<uint32_t*>(texture + GameLayout::Texture::kRefCount) = 1;

    std::ostringstream oss;
    oss << "uiclone TextureDX12 copy: src=" << HookUtils::HexU64(textureDx12)
        << " clone=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(textureDx12Copy))
        << " size=" << cloneSize
        << " relocated=" << relocated
        << " ext38Relocated=" << relocatedExt38;
    logger.Log(oss.str());

    uint64_t sourceTextureDx12 = 0;
    if (CustomJacketInternal::TryGetSourceJacketTextureDx12(sourceTextureDx12, logger))
    {
        CustomJacketInternal::TryBindTextureDx12CloneWrapperToNewResource(
            reinterpret_cast<uint64_t>(textureDx12Copy), sourceTextureDx12,
            "DefaultConstructionHoloImageTexture", logger);
    }
    return texture;
}

uint8_t* CloneUiTexture(uint64_t loaded, uint8_t* texture, const Logger& logger)
{
    auto* ui = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x100));
    if (!ui)
    {
        logger.Log("uiclone: alloc UITexture failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(ui, reinterpret_cast<void*>(loaded), 0x100))
    {
        logger.Log("uiclone: memcpy UITexture failed");
        return nullptr;
    }

    SpecialTrackHelpers::ResetObjectHeader(ui);
    if (!CustomJacketInternal::SehWritePtrVal(
            ui, GameLayout::UiTexture::kTexture, texture)
        || !CustomJacketInternal::SehWritePtrVal(
            ui, GameLayout::UiTexture::kSelf, ui))
    {
        logger.Log("uiclone: UITexture wire failed");
        return nullptr;
    }
    return ui;
}

uint8_t* BuildLoadedTarget(uint64_t originalTarget, uint8_t* loaded, const Logger& logger)
{
    auto* target = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x30));
    if (!target)
    {
        logger.Log("uiclone: alloc target failed");
        return nullptr;
    }

    uint64_t resource = 0;
    CustomJacketInternal::SehReadU64(originalTarget, resource);
    auto* q = reinterpret_cast<uint64_t*>(target);
    q[0] = resource;
    q[1] = 0xFFFFFFFFull;
    const uint32_t tick = GetTickCount();
    q[2] = 0xAD90000100000000ull | tick;
    q[3] = 0xAD90000100000000ull | (tick ^ 0x13579BDFu);
    *reinterpret_cast<void**>(
        target + GameLayout::StreamingTarget::kLoaded) = loaded;
    q[5] = 1;
    return target;
}
} // namespace

namespace CustomJacketInternal
{
bool CloneLoadedUiTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger)
{
    newTarget = 0;
    if (!track) return false;

    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadCurrentLoaded(track, slotAddr, slot, loaded, texture))
    {
        logger.Log("uiclone: source target not loaded yet");
        return false;
    }

    auto* newTexture = CloneTexture(texture, logger);
    if (!newTexture) return false;

    auto* newUi = CloneUiTexture(loaded, newTexture, logger);
    if (!newUi) return false;

    auto* target = BuildLoadedTarget(slot.target, newUi, logger);
    if (!target) return false;

    const uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
    if (!SehAssignLoaded(reinterpret_cast<void*>(ctx),
        reinterpret_cast<uint64_t**>(
            static_cast<uint8_t*>(track) + GameLayout::Track::kJacket),
        newUi, target))
    {
        logger.Log("uiclone: assign failed");
        return false;
    }

    newTarget = reinterpret_cast<uint64_t>(target);
    std::ostringstream oss;
    oss << "uiclone OK: srcSlot=" << HookUtils::HexU64(slotAddr)
        << " srcTarget=" << HookUtils::HexU64(slot.target)
        << " newTarget=" << HookUtils::HexU64(newTarget)
        << " newUI=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newUi))
        << " srcTexture=" << HookUtils::HexU64(texture)
        << " newTexture=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newTexture));
    logger.Log(oss.str());
    return true;
}

} // namespace CustomJacketInternal
