#pragma once

#include "Logger.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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
bool SehTriggerDetachedLoad(const CustomJacketSlot& slot);
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
void DumpPixelBufferComparisonOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, uint64_t cloneSize, const Logger& logger);
void DumpPixelBufferExternalBlocksOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, uint64_t cloneSize, const Logger& logger);
void ResetPixelBufferGpuResourceDiagnostics();
void DumpPixelBufferHandleSlots(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, const Logger& logger);
void DumpPixelBufferGpuResourceOnce(uint64_t hotPB, uint64_t noDataPB,
    uint64_t clonePB, const Logger& logger);
bool TryBindTextureDx12ToSourceWrapper(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger);
bool TryBindTextureDx12CloneWrapperToSourceResource(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger);
bool TryCreateCustomJacketD3D12ResourceLike(uint64_t sourceResource,
    uint64_t& outResource, const Logger& logger);
bool TryUploadCustomJacketD3D12TestPattern(uint64_t resource, const Logger& logger);
bool TryDecodeCustomJacketImageToRgba(const uint8_t* encoded, uint32_t encodedBytes,
    uint32_t targetW, uint32_t targetH, std::vector<uint8_t>& rgba,
    uint32_t& sourceW, uint32_t& sourceH, uint32_t& drawW, uint32_t& drawH,
    const Logger& logger);
bool TryUploadCustomJacketD3D12Pixels(uint64_t resource, const uint8_t* rgba,
    uint32_t width, uint32_t height, const Logger& logger);
bool TryEncodeExternalBc7ToRows(uint8_t* dst, uint32_t dstW, uint32_t dstH,
    uint32_t rowPitch, const uint8_t* rgba, uint32_t srcW, uint32_t srcH,
    const Logger& logger);
void FillFallbackBc7FromRgba(uint8_t* dst, uint64_t dstOffset,
    uint32_t dstW, uint32_t dstH, uint32_t rowPitch,
    const uint8_t* rgba, uint32_t srcW, uint32_t srcH);
void SetActiveCustomJacketD3D12Resource(uint64_t resource);
uint64_t GetActiveCustomJacketD3D12Resource();
bool TryBindTextureDx12CloneWrapperToNewResource(uint64_t textureDx12,
    uint64_t sourceTextureDx12, const char* label, const Logger& logger);
int OverwriteDXBCPages(uint8_t* pixelBuffer, size_t sizeBytes, const Logger& logger);
bool CloneAndReplacePixelBuffer(void* track, bool& replaced, const Logger& logger);
bool CloneLoadedUiTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger);
bool CloneLoadedUiTextureWithTextureOverrideToTrack(void* track,
    uint64_t overrideTexture, const char* label, uint64_t& newTarget, const Logger& logger);
bool CloneLoadedUiTextureWithPixelBufferOverrideToTrack(void* track,
    uint64_t overridePixelBuffer, const char* label, uint64_t& newTarget, const Logger& logger);
void ResetAlternateJacketTextureProbe();
bool PrepareAlternateJacketTextureProbe(void* catalogueResource, const Logger& logger);
bool TryGetAlternateJacketPixelBuffer(uint64_t& pixelBuffer, const Logger& logger);
bool TryCloneAlternateJacketTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger);
bool TryCloneAlternatePixelBufferToTrack(void* track, uint64_t& newTarget, const Logger& logger);
uint8_t* CloneAndPatchPixelBufferForUiClone(uint64_t source,
    uint64_t& cloneSize, uint32_t& relocated, uint32_t& patchedPages, const Logger& logger);
} // namespace CustomJacketInternal
