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

bool TryCreateCustomJacketD3D12ResourceLike(uint64_t sourceResource,
    uint64_t& outResource, const Logger& logger);
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
bool CloneLoadedUiTextureToTrack(void* track, uint64_t& newTarget, const Logger& logger);
void ResetSourceJacketTexture();
bool PrepareSourceJacketTexture(void* catalogueResource, const Logger& logger);
bool TryGetSourceJacketTextureDx12(uint64_t& textureDx12, const Logger& logger);
uint8_t* ClonePixelBufferForUiClone(uint64_t source,
    uint64_t& cloneSize, uint32_t& relocated, uint32_t& relocatedExt38,
    const Logger& logger);
} // namespace CustomJacketInternal
