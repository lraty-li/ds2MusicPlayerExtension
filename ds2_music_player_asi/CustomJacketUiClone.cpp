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

bool ReadTexturePixelBuffer(uint64_t texture, uint64_t& pixelBuffer)
{
    __try
    {
        pixelBuffer = *reinterpret_cast<uint64_t*>(texture + 0x20);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        pixelBuffer = 0;
        return false;
    }
}

uint8_t* CloneTexture(uint64_t sourceTexture, const Logger& logger)
{
    uint64_t pixelBuffer = 0;
    if (!ReadTexturePixelBuffer(sourceTexture, pixelBuffer) || !pixelBuffer)
    {
        logger.Log("uiclone: source Texture has no pixelBuffer");
        return nullptr;
    }

    uint64_t cloneSize = 0;
    uint32_t relocated = 0;
    uint32_t patchedPages = 0;
    auto* pixelBufferCopy = CustomJacketInternal::CloneAndPatchPixelBufferForUiClone(pixelBuffer,
        cloneSize, relocated, patchedPages, logger);
    if (!pixelBufferCopy) return nullptr;

    auto* texture = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x600));
    if (!texture)
    {
        logger.Log("uiclone: alloc Texture failed");
        VirtualFree(pixelBufferCopy, 0, MEM_RELEASE);
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(
        texture, reinterpret_cast<void*>(sourceTexture), 0x70))
    {
        logger.Log("uiclone: memcpy Texture header failed");
        VirtualFree(pixelBufferCopy, 0, MEM_RELEASE);
        return nullptr;
    }

    *reinterpret_cast<uint64_t*>(texture + 0x20) = reinterpret_cast<uint64_t>(pixelBufferCopy);
    *reinterpret_cast<uint64_t*>(texture + 0x70) = reinterpret_cast<uint64_t>(texture + 0xE0);
    *reinterpret_cast<uint64_t*>(texture + 0xE0) = reinterpret_cast<uint64_t>(texture + 0x150);
    *reinterpret_cast<uint64_t*>(texture + 0x150) = reinterpret_cast<uint64_t>(texture + 0x1C0);
    *reinterpret_cast<uint64_t*>(texture + 0x1C0) = 0;
    *reinterpret_cast<uint32_t*>(texture + 0x08) = 1;

    std::ostringstream oss;
    oss << "uiclone PB copy: srcPB=" << HookUtils::HexU64(pixelBuffer)
        << " newPB=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(pixelBufferCopy))
        << " size=" << cloneSize
        << " relocated=" << relocated
        << " patchedPages=" << patchedPages;
    logger.Log(oss.str());

    uint64_t noDataPixelBuffer = 0;
    if (CustomJacketInternal::TryGetAlternateJacketPixelBuffer(noDataPixelBuffer, logger))
    {
        CustomJacketInternal::DumpPixelBufferComparisonOnce(pixelBuffer,
            noDataPixelBuffer, reinterpret_cast<uint64_t>(pixelBufferCopy), cloneSize, logger);
        CustomJacketInternal::DumpPixelBufferExternalBlocksOnce(pixelBuffer,
            noDataPixelBuffer, reinterpret_cast<uint64_t>(pixelBufferCopy), cloneSize, logger);
        CustomJacketInternal::DumpPixelBufferGpuResourceOnce(pixelBuffer,
            noDataPixelBuffer, reinterpret_cast<uint64_t>(pixelBufferCopy), logger);
        CustomJacketInternal::TryBindTextureDx12CloneWrapperToNewResource(
            reinterpret_cast<uint64_t>(pixelBufferCopy), noDataPixelBuffer,
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
    if (!CustomJacketInternal::SehWritePtrVal(ui, 0x30, texture)
        || !CustomJacketInternal::SehWritePtrVal(ui, 0x38, ui))
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
    *reinterpret_cast<void**>(target + 0x20) = loaded;
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
        reinterpret_cast<uint64_t**>(static_cast<uint8_t*>(track) + 0x50), newUi, target))
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

bool CloneLoadedUiTextureWithTextureOverrideToTrack(void* track,
    uint64_t overrideTexture, const char* label, uint64_t& newTarget, const Logger& logger)
{
    newTarget = 0;
    if (!track || !overrideTexture) return false;

    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    uint64_t loaded = 0;
    uint64_t texture = 0;
    if (!ReadCurrentLoaded(track, slotAddr, slot, loaded, texture))
    {
        logger.Log("uiclone alt: source target not loaded yet");
        return false;
    }

    auto* newUi = CloneUiTexture(loaded, reinterpret_cast<uint8_t*>(overrideTexture), logger);
    if (!newUi) return false;

    auto* target = BuildLoadedTarget(slot.target, newUi, logger);
    if (!target) return false;

    const uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
    if (!SehAssignLoaded(reinterpret_cast<void*>(ctx),
        reinterpret_cast<uint64_t**>(static_cast<uint8_t*>(track) + 0x50), newUi, target))
    {
        logger.Log("uiclone alt: assign failed");
        return false;
    }

    newTarget = reinterpret_cast<uint64_t>(target);
    std::ostringstream oss;
    oss << "uiclone alt OK: label=" << (label ? label : "")
        << " srcSlot=" << HookUtils::HexU64(slotAddr)
        << " srcTarget=" << HookUtils::HexU64(slot.target)
        << " newTarget=" << HookUtils::HexU64(newTarget)
        << " newUI=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newUi))
        << " srcTexture=" << HookUtils::HexU64(texture)
        << " overrideTexture=" << HookUtils::HexU64(overrideTexture);
    logger.Log(oss.str());
    return true;
}
} // namespace CustomJacketInternal
