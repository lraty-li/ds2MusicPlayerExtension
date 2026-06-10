#include "pch.h"

#include "SpecialTrackSoundClone.h"

#include "DecimaTypes.h"
#include "SpecialTrackHelpers.h"
#include "SpecialTrackIds.h"

namespace
{
constexpr size_t kWwiseIdCloneSize = 0x30;
constexpr size_t kNcrCloneSize = 0xC0;
constexpr size_t kGprCloneSize = 0x100;
constexpr size_t kGsrCloneSize = 0x300;

constexpr uint32_t kGsrGraphProgramOffset = 0x288;
constexpr uint32_t kGprExposedDataOffset = 0x0B8;
constexpr uint32_t kNcrDsloOffset = 0x40;
constexpr uint32_t kWwiseIdIdOffset = 0x20;
} // namespace

namespace SpecialTrackSoundClone
{
uint32_t ReadEventIdFromGsr(void* gsr)
{
    __try
    {
        void* gpr = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(gsr) + kGsrGraphProgramOffset);
        void* ncr = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(gpr) + kGprExposedDataOffset);
        auto* dslo = reinterpret_cast<RawArray*>(
            static_cast<uint8_t*>(ncr) + kNcrDsloOffset);
        if (!dslo->entries || dslo->count == 0 || !dslo->entries[0])
        {
            return 0;
        }
        return *reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(dslo->entries[0]) + kWwiseIdIdOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool Build(void* sourceGsr, SpecialTrackCloneChainResult& result)
{
    __try
    {
        void* sourceGpr = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(sourceGsr) + kGsrGraphProgramOffset);
        void* sourceNcr = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(sourceGpr) + kGprExposedDataOffset);
        auto* sourceDslo = reinterpret_cast<RawArray*>(
            static_cast<uint8_t*>(sourceNcr) + kNcrDsloOffset);
        if (!sourceDslo->entries || sourceDslo->count == 0 || !sourceDslo->entries[0])
        {
            return false;
        }

        result.gsr = SpecialTrackHelpers::HeapAllocZero(kGsrCloneSize);
        result.gpr = SpecialTrackHelpers::HeapAllocZero(kGprCloneSize);
        result.ncr = SpecialTrackHelpers::HeapAllocZero(kNcrCloneSize);
        result.wwiseId = SpecialTrackHelpers::HeapAllocZero(kWwiseIdCloneSize);
        result.dsloEntries = static_cast<void**>(SpecialTrackHelpers::HeapAllocZero(
            static_cast<size_t>(sourceDslo->count) * sizeof(void*)));
        if (!result.gsr || !result.gpr || !result.ncr ||
            !result.wwiseId || !result.dsloEntries)
        {
            return false;
        }

        memcpy(result.gsr, sourceGsr, kGsrCloneSize);
        memcpy(result.gpr, sourceGpr, kGprCloneSize);
        memcpy(result.ncr, sourceNcr, kNcrCloneSize);
        memcpy(result.wwiseId, sourceDslo->entries[0], kWwiseIdCloneSize);
        memcpy(result.dsloEntries, sourceDslo->entries,
            static_cast<size_t>(sourceDslo->count) * sizeof(void*));

        SpecialTrackHelpers::ResetObjectHeader(result.gsr);
        SpecialTrackHelpers::ResetObjectHeader(result.gpr);
        SpecialTrackHelpers::ResetObjectHeader(result.ncr);
        SpecialTrackHelpers::ResetObjectHeader(result.wwiseId);

        result.oldEventId = *reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(result.wwiseId) + kWwiseIdIdOffset);
        *reinterpret_cast<uint32_t*>(
            static_cast<uint8_t*>(result.wwiseId) + kWwiseIdIdOffset) =
            SpecialTrackIds::kCustomEventId;

        result.dsloEntries[0] = result.wwiseId;
        auto* newDslo = reinterpret_cast<RawArray*>(
            static_cast<uint8_t*>(result.ncr) + kNcrDsloOffset);
        newDslo->count = sourceDslo->count;
        newDslo->capacity = sourceDslo->count;
        newDslo->entries = result.dsloEntries;

        *reinterpret_cast<void**>(
            static_cast<uint8_t*>(result.gpr) + kGprExposedDataOffset) = result.ncr;
        *reinterpret_cast<void**>(
            static_cast<uint8_t*>(result.gsr) + kGsrGraphProgramOffset) = result.gpr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace SpecialTrackSoundClone
