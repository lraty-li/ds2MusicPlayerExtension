#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"
#include "SpecialTrackHelpers.h"

#include <sstream>

namespace
{
bool ReadCurrentLoaded(void* track, uint64_t& slotAddr,
    CustomJacketSlot& slot, uint64_t& loaded, uint64_t& texture)
{
    auto* bytes = static_cast<uint8_t*>(track);
    if (!CustomJacketInternal::SehReadU64(reinterpret_cast<uint64_t>(bytes + 0x50), slotAddr)
        || !slotAddr
        || !CustomJacketInternal::SehReadSlot(slotAddr, slot)
        || !CustomJacketInternal::SehReadU64(slot.target + 0x20, loaded)
        || !loaded)
    {
        return false;
    }
    return CustomJacketInternal::SehReadU64(loaded + 0x30, texture) && texture;
}

uint8_t* CloneTextureWithPixelBuffer(uint64_t sourceTexture,
    uint64_t pixelBuffer, const char* label, const Logger& logger)
{
    auto* texture = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x600));
    if (!texture)
    {
        logger.Log("uiclone altpb: alloc Texture failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(
        texture, reinterpret_cast<void*>(sourceTexture), 0x70))
    {
        logger.Log("uiclone altpb: memcpy Texture header failed");
        return nullptr;
    }

    *reinterpret_cast<uint64_t*>(texture + 0x20) = pixelBuffer;
    *reinterpret_cast<uint64_t*>(texture + 0x70) = reinterpret_cast<uint64_t>(texture + 0xE0);
    *reinterpret_cast<uint64_t*>(texture + 0xE0) = reinterpret_cast<uint64_t>(texture + 0x150);
    *reinterpret_cast<uint64_t*>(texture + 0x150) = reinterpret_cast<uint64_t>(texture + 0x1C0);
    *reinterpret_cast<uint64_t*>(texture + 0x1C0) = 0;
    *reinterpret_cast<uint32_t*>(texture + 0x08) = 1;

    std::ostringstream oss;
    oss << "uiclone altpb Texture: label=" << (label ? label : "")
        << " srcTexture=" << HookUtils::HexU64(sourceTexture)
        << " newTexture=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(texture))
        << " overridePB=" << HookUtils::HexU64(pixelBuffer);
    logger.Log(oss.str());
    return texture;
}

uint8_t* CloneUiTexture(uint64_t loaded, uint8_t* texture, const Logger& logger)
{
    auto* ui = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x100));
    if (!ui)
    {
        logger.Log("uiclone altpb: alloc UITexture failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(ui, reinterpret_cast<void*>(loaded), 0x100))
    {
        logger.Log("uiclone altpb: memcpy UITexture failed");
        return nullptr;
    }

    SpecialTrackHelpers::ResetObjectHeader(ui);
    if (!CustomJacketInternal::SehWritePtrVal(ui, 0x30, texture)
        || !CustomJacketInternal::SehWritePtrVal(ui, 0x38, ui))
    {
        logger.Log("uiclone altpb: UITexture wire failed");
        return nullptr;
    }
    return ui;
}

uint8_t* BuildLoadedTarget(uint64_t originalTarget, uint8_t* loaded, const Logger& logger)
{
    auto* target = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x30));
    if (!target)
    {
        logger.Log("uiclone altpb: alloc target failed");
        return nullptr;
    }

    uint64_t resource = 0;
    CustomJacketInternal::SehReadU64(originalTarget, resource);
    auto* q = reinterpret_cast<uint64_t*>(target);
    q[0] = resource;
    q[1] = 0xFFFFFFFFull;
    const uint32_t tick = GetTickCount();
    q[2] = 0xAD90000100000000ull | tick;
    q[3] = 0xAD90000100000000ull | (tick ^ 0x2468ACE0u);
    *reinterpret_cast<void**>(target + 0x20) = loaded;
    q[5] = 1;
    return target;
}
} // namespace

namespace CustomJacketInternal
{
bool CloneLoadedUiTextureWithPixelBufferOverrideToTrack(void* track,
    uint64_t overridePixelBuffer, const char* label, uint64_t& newTarget, const Logger& logger)
{
    newTarget = 0;
    if (!track || !overridePixelBuffer) return false;

    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadCurrentLoaded(track, slotAddr, slot, loaded, texture))
    {
        logger.Log("uiclone altpb: source target not loaded yet");
        return false;
    }

    auto* newTexture = CloneTextureWithPixelBuffer(texture, overridePixelBuffer, label, logger);
    if (!newTexture) return false;

    auto* newUi = CloneUiTexture(loaded, newTexture, logger);
    if (!newUi) return false;

    auto* target = BuildLoadedTarget(slot.target, newUi, logger);
    if (!target) return false;

    const uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
    if (!SehAssignLoaded(reinterpret_cast<void*>(ctx),
        reinterpret_cast<uint64_t**>(static_cast<uint8_t*>(track) + 0x50), newUi, target))
    {
        logger.Log("uiclone altpb: assign failed");
        return false;
    }

    newTarget = reinterpret_cast<uint64_t>(target);
    std::ostringstream oss;
    oss << "uiclone altpb OK: label=" << (label ? label : "")
        << " srcSlot=" << HookUtils::HexU64(slotAddr)
        << " srcTarget=" << HookUtils::HexU64(slot.target)
        << " newTarget=" << HookUtils::HexU64(newTarget)
        << " newUI=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newUi))
        << " srcTexture=" << HookUtils::HexU64(texture)
        << " newTexture=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newTexture))
        << " overridePB=" << HookUtils::HexU64(overridePixelBuffer);
    logger.Log(oss.str());
    return true;
}
} // namespace CustomJacketInternal
