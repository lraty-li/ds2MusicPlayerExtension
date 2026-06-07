#include "pch.h"

#include "CustomJacketInternal.h"

#include "HookUtils.h"
#include "SpecialTrackHelpers.h"

#include <sstream>

namespace
{
bool ReadTrackJacketSlot(void* track, uint64_t& slotAddr, CustomJacketSlot& slot)
{
    auto* bytes = static_cast<uint8_t*>(track);
    if (!CustomJacketInternal::SehReadU64(reinterpret_cast<uint64_t>(bytes + 0x50), slotAddr))
    {
        return false;
    }
    return slotAddr && CustomJacketInternal::SehReadSlot(slotAddr, slot);
}

bool ReadLoadedTexture(uint64_t targetAddr, uint64_t& loaded, uint64_t& texture)
{
    loaded = 0;
    texture = 0;
    if (!CustomJacketInternal::SehReadU64(targetAddr + 0x20, loaded) || !loaded)
    {
        return false;
    }
    auto* ui = reinterpret_cast<uint8_t*>(loaded);
    return CustomJacketInternal::SehReadU64(reinterpret_cast<uint64_t>(ui + 0x30), texture)
        && texture;
}

void LogPixelBufferInfo(const Logger& logger, uint64_t pb,
    const CustomJacketPixelBufferInfo& info)
{
    std::ostringstream oss;
    oss << "bcn: origPB=" << HookUtils::HexU64(pb)
        << " readable=" << info.readableSize
        << " clone=" << info.cloneSize
        << " tiles=" << info.pageTableWidth << "x" << info.pageTableHeight
        << " dxbcMarkers=" << info.dxbcMarkers
        << " dxbcPages=" << info.dxbcPages;
    logger.Log(oss.str());
}

uint8_t* ClonePixelBuffer(uint8_t* source, size_t sizeBytes, const Logger& logger)
{
    auto* copy = static_cast<uint8_t*>(VirtualAlloc(nullptr, sizeBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copy)
    {
        logger.Log("bcn: VirtualAlloc PB failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(copy, source, sizeBytes))
    {
        logger.Log("bcn: memcpy PB failed");
        VirtualFree(copy, 0, MEM_RELEASE);
        return nullptr;
    }
    return copy;
}

uint8_t* CloneTexture(uint8_t* original, uint8_t* newPixelBuffer, const Logger& logger)
{
    auto* texture = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x600));
    if (!texture)
    {
        logger.Log("bcn: alloc Texture failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(texture, original, 0x70))
    {
        logger.Log("bcn: memcpy Texture header failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehWritePtrVal(texture, 0x20, newPixelBuffer))
    {
        logger.Log("bcn: Texture+0x20 wire failed");
        return nullptr;
    }

    *reinterpret_cast<uint64_t*>(texture + 0x70) = reinterpret_cast<uint64_t>(texture + 0xE0);
    *reinterpret_cast<uint64_t*>(texture + 0xE0) = reinterpret_cast<uint64_t>(texture + 0x150);
    *reinterpret_cast<uint64_t*>(texture + 0x150) = reinterpret_cast<uint64_t>(texture + 0x1C0);
    *reinterpret_cast<uint64_t*>(texture + 0x1C0) = 0;
    *reinterpret_cast<uint32_t*>(texture + 0x08) = 1;
    return texture;
}

uint8_t* CloneUiTexture(uint8_t* original, uint8_t* texture, const Logger& logger)
{
    auto* ui = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x100));
    if (!ui)
    {
        logger.Log("bcn: alloc UITexture failed");
        return nullptr;
    }
    if (!CustomJacketInternal::SehMemcpySafe(ui, original, 0x100))
    {
        logger.Log("bcn: memcpy UITexture failed");
        return nullptr;
    }
    SpecialTrackHelpers::ResetObjectHeader(ui);
    if (!CustomJacketInternal::SehWritePtrVal(ui, 0x30, texture)
        || !CustomJacketInternal::SehWritePtrVal(ui, 0x38, ui))
    {
        logger.Log("bcn: UITexture wire failed");
        return nullptr;
    }
    return ui;
}

uint8_t* BuildLoadedTarget(uint64_t originalTarget, uint8_t* loaded, const Logger& logger)
{
    auto* target = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(0x30));
    if (!target)
    {
        logger.Log("bcn: alloc target failed");
        return nullptr;
    }

    auto* q = reinterpret_cast<uint64_t*>(target);
    q[0] = *reinterpret_cast<uint64_t*>(originalTarget);
    q[1] = 0xFFFFFFFF;
    const uint32_t tick = GetTickCount();
    q[2] = 0xAD90000100000000ULL | tick;
    q[3] = 0xAD90000100000000ULL | (tick ^ 0x87654321);
    *reinterpret_cast<void**>(target + 0x20) = loaded;
    q[5] = 1;
    return target;
}
} // namespace

namespace CustomJacketInternal
{
bool CloneAndReplacePixelBuffer(void* track, bool& replaced, const Logger& logger)
{
    if (!track || replaced) return false;

    uint64_t slotAddr = 0;
    CustomJacketSlot slot = {};
    if (!ReadTrackJacketSlot(track, slotAddr, slot)) return false;

    uint64_t loaded = 0;
    uint64_t textureAddr = 0;
    if (!ReadLoadedTexture(slot.target, loaded, textureAddr))
    {
        logger.Log("bcn: target not loaded yet");
        return false;
    }
    DumpResourceJacketProbeOnce(slotAddr, slot, loaded, textureAddr, logger);

    auto* originalUi = reinterpret_cast<uint8_t*>(loaded);
    auto* originalTexture = reinterpret_cast<uint8_t*>(textureAddr);
    uint64_t pixelBufferAddr = 0;
    if (!SehReadU64(reinterpret_cast<uint64_t>(originalTexture + 0x20), pixelBufferAddr)
        || !pixelBufferAddr)
    {
        logger.Log("bcn: no orig pixelBuffer");
        return false;
    }

    CustomJacketPixelBufferInfo info = {};
    if (!ProbePixelBuffer(pixelBufferAddr, info))
    {
        logger.Log("bcn: failed to probe PB info");
        return false;
    }
    LogPixelBufferInfo(logger, pixelBufferAddr, info);
    if (info.dxbcPages)
    {
        DumpPixelBufferLayoutOnce(pixelBufferAddr, info, logger);
        DumpDXBCPageHeadersOnce(pixelBufferAddr, info, logger);
        logger.Log("bcn: probe-only mode; DXBC pages detected, replacement skipped");
        return false;
    }
    if (!info.dxbcPages || info.cloneSize < 0x10000)
    {
        DumpPixelBufferLayoutOnce(pixelBufferAddr, info, logger);
        logger.Log("bcn: no DXBC pages found");
        return false;
    }

    auto* newPb = ClonePixelBuffer(reinterpret_cast<uint8_t*>(pixelBufferAddr),
        static_cast<size_t>(info.cloneSize), logger);
    if (!newPb) return false;

    const int pageCount = OverwriteDXBCPages(newPb, static_cast<size_t>(info.cloneSize), logger);
    logger.Log(std::string("bcn: overwrote ") + std::to_string(pageCount) + " DXBC pages");
    if (!pageCount)
    {
        VirtualFree(newPb, 0, MEM_RELEASE);
        return false;
    }

    auto* newTexture = CloneTexture(originalTexture, newPb, logger);
    if (!newTexture) return false;

    auto* newUi = CloneUiTexture(originalUi, newTexture, logger);
    if (!newUi) return false;

    auto* newTarget = BuildLoadedTarget(slot.target, newUi, logger);
    if (!newTarget) return false;

    const uint64_t ctx = slot.packed & 0x000FFFFFFFFFFFFFULL;
    if (!SehAssignLoaded(reinterpret_cast<void*>(ctx),
        reinterpret_cast<uint64_t**>(static_cast<uint8_t*>(track) + 0x50), newUi, newTarget))
    {
        logger.Log("bcn: assign failed");
        return false;
    }

    replaced = true;
    std::ostringstream oss;
    oss << "bcn OK: ui=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newUi))
        << " tex=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newTexture))
        << " pb=" << HookUtils::HexU64(reinterpret_cast<uint64_t>(newPb))
        << " pages=" << pageCount;
    logger.Log(oss.str());
    return true;
}
} // namespace CustomJacketInternal
