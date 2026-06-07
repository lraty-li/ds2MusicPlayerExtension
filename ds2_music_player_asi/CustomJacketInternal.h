#pragma once

#include "Logger.h"

#include <cstddef>
#include <cstdint>

struct CustomJacketSlot
{
    uint64_t target;
    uint64_t packed;
};

struct CustomJacketPixelBufferInfo
{
    uint64_t readableSize;
    uint64_t cloneSize;
    uint32_t pageTableWidth;
    uint32_t pageTableHeight;
    uint32_t dxbcMarkers;
    uint32_t dxbcPages;
};

namespace CustomJacketInternal
{
bool SehReadU64(uint64_t addr, uint64_t& out);
bool SehMemcpySafe(void* dst, const void* src, size_t size);
bool SehWritePtrVal(uint8_t* base, int32_t offset, void* val);
bool SehReadSlot(uint64_t slotAddr, CustomJacketSlot& out);
bool SehCopySlotToTrack(uint8_t* track, const CustomJacketSlot& src);
bool SehTriggerLoad(uint8_t* track, const CustomJacketSlot& slot);
bool SehAssignLoaded(void* ctx, uint64_t** slotPtr, void* loadedObj, void* target);

bool ProbePixelBuffer(uint64_t pbAddr, CustomJacketPixelBufferInfo& out);
void DumpDXBCPageHeadersOnce(uint64_t pbAddr,
    const CustomJacketPixelBufferInfo& info, const Logger& logger);
void ResetDXBCPageHeaderDiagnostics();
void ResetResourceProbeDiagnostics();
void ResetTrackAlbumProbeDiagnostics();
void ResetPixelBufferDiagnostics();
void DumpCatalogueResourceProbeOnce(void* catalogueResource, const Logger& logger);
void DumpTrackAlbumProbeOnce(void* track, const Logger& logger);
void DumpResourceJacketProbeOnce(uint64_t slotAddr, const CustomJacketSlot& slot,
    uint64_t loaded, uint64_t texture, const Logger& logger);
void DumpPixelBufferLayoutOnce(uint64_t pbAddr,
    const CustomJacketPixelBufferInfo& info, const Logger& logger);
void DumpPixelBufferPointersOnce(uint64_t pbAddr, const Logger& logger);
int OverwriteDXBCPages(uint8_t* pixelBuffer, size_t sizeBytes, const Logger& logger);
bool CloneAndReplacePixelBuffer(void* track, bool& replaced, const Logger& logger);
bool CloneLoadedUiTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger);
} // namespace CustomJacketInternal
