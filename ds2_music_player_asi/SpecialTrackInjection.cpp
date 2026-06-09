#include "pch.h"

#include "SpecialTrackInjection.h"

#include "CustomJacketInstaller.h"
#include "DynamicTrackTitleSync.h"
#include "SpecialTrackIds.h"
#include "SpecialTrackHelpers.h"

#include <sstream>

namespace
{
constexpr uint32_t kRejectedSentinelEvent = 82u;

constexpr size_t kTrackCloneSize = 0x300;
constexpr size_t kAlbumCloneSize = 0x80;
constexpr size_t kWwiseIdCloneSize = 0x30;
constexpr size_t kNcrCloneSize = 0xC0;
constexpr size_t kGprCloneSize = 0x100;
constexpr size_t kGsrCloneSize = 0x300;

constexpr uint32_t kGsrGraphProgramOffset = 0x288;
constexpr uint32_t kGprExposedDataOffset = 0x0B8;
constexpr uint32_t kNcrDsloOffset = 0x40;
constexpr uint32_t kWwiseIdIdOffset = 0x20;

struct CloneChainResult
{
    void* gsr = nullptr;
    void* gpr = nullptr;
    void* ncr = nullptr;
    void* wwiseId = nullptr;
    void** dsloEntries = nullptr;
    uint32_t oldEventId = 0;
};

Logger* g_logger = nullptr;
bool g_injected = false;

void Log(const std::string& text)
{
    if (g_logger)
    {
        g_logger->Log(text);
    }
}

uint32_t ReadEventIdFromGsr(void* gsr)
{
    __try
    {
        void* gpr = *reinterpret_cast<void**>(static_cast<uint8_t*>(gsr) + kGsrGraphProgramOffset);
        void* ncr = *reinterpret_cast<void**>(static_cast<uint8_t*>(gpr) + kGprExposedDataOffset);
        auto* dslo = reinterpret_cast<RawArray*>(static_cast<uint8_t*>(ncr) + kNcrDsloOffset);
        if (!dslo->entries || dslo->count == 0 || !dslo->entries[0])
        {
            return 0;
        }
        return *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(dslo->entries[0]) + kWwiseIdIdOffset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool BuildClonedChain(void* sourceGsr, CloneChainResult& result)
{
    __try
    {
        void* sourceGpr =
            *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceGsr) + kGsrGraphProgramOffset);
        void* sourceNcr =
            *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceGpr) + kGprExposedDataOffset);
        auto* sourceDslo =
            reinterpret_cast<RawArray*>(static_cast<uint8_t*>(sourceNcr) + kNcrDsloOffset);
        if (!sourceDslo->entries || sourceDslo->count == 0 || !sourceDslo->entries[0])
        {
            return false;
        }

        result.gsr = SpecialTrackHelpers::HeapAllocZero(kGsrCloneSize);
        result.gpr = SpecialTrackHelpers::HeapAllocZero(kGprCloneSize);
        result.ncr = SpecialTrackHelpers::HeapAllocZero(kNcrCloneSize);
        result.wwiseId = SpecialTrackHelpers::HeapAllocZero(kWwiseIdCloneSize);
        result.dsloEntries = static_cast<void**>(
            SpecialTrackHelpers::HeapAllocZero(static_cast<size_t>(sourceDslo->count) * sizeof(void*)));
        if (!result.gsr || !result.gpr || !result.ncr || !result.wwiseId || !result.dsloEntries)
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

        result.oldEventId =
            *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(result.wwiseId) + kWwiseIdIdOffset);
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(result.wwiseId) + kWwiseIdIdOffset) =
            SpecialTrackIds::kCustomEventId;

        result.dsloEntries[0] = result.wwiseId;
        auto* newDslo = reinterpret_cast<RawArray*>(
            static_cast<uint8_t*>(result.ncr) + kNcrDsloOffset);
        newDslo->count = sourceDslo->count;
        newDslo->capacity = sourceDslo->count;
        newDslo->entries = result.dsloEntries;

        *reinterpret_cast<void**>(static_cast<uint8_t*>(result.gpr) + kGprExposedDataOffset) =
            result.ncr;
        *reinterpret_cast<void**>(static_cast<uint8_t*>(result.gsr) + kGsrGraphProgramOffset) =
            result.gpr;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void* CloneTrack(void* sourceTrack, void* sourceText, CloneChainResult& chain)
{
    auto* object = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(kTrackCloneSize));
    if (!object)
    {
        return nullptr;
    }

    memcpy(object, sourceTrack, kTrackCloneSize);
    SpecialTrackHelpers::ResetObjectHeader(object);
    *reinterpret_cast<uint32_t*>(object + 0x20) = SpecialTrackIds::kCustomTrackId;
    *reinterpret_cast<uint16_t*>(object + 0x24) = 3600;
    *reinterpret_cast<int16_t*>(object + 0x26) = 30000;
    *reinterpret_cast<uint8_t*>(object + 0x28) = 1;
    *reinterpret_cast<void**>(object + 0x38) =
        SpecialTrackHelpers::CreateLocalizedText("!!! External Stream Source", sourceText);
    *reinterpret_cast<void**>(object + 0x40) = chain.gsr;
    *reinterpret_cast<void**>(object + 0x48) = chain.gsr;
    *reinterpret_cast<void**>(object + 0x58) = nullptr;
    return object;
}

void* CloneAlbum(void* sourceAlbum, void* sourceText)
{
    if (!sourceAlbum) return nullptr;
    auto* object = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(kAlbumCloneSize));
    if (!object) return nullptr;
    memcpy(object, sourceAlbum, kAlbumCloneSize);
    SpecialTrackHelpers::ResetObjectHeader(object);
    void* artistText = *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceAlbum) + 0x30);
    void* telopText = *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceAlbum) + 0x40);
    *reinterpret_cast<void**>(object + 0x30) =
        SpecialTrackHelpers::CreateLocalizedText("", artistText ? artistText : sourceText);
    *reinterpret_cast<void**>(object + 0x40) =
        SpecialTrackHelpers::CreateLocalizedText("", telopText ? telopText : sourceText);
    return object;
}

void* PickSourceTrack(RawArray* trackArray, uint32_t& outIndex, uint32_t& outEventId)
{
    void* fallback = nullptr;
    uint32_t fallbackIndex = 0;
    uint32_t fallbackEvent = 0;
    outIndex = 0;
    outEventId = 0;

    for (uint32_t i = 1; i < trackArray->count; ++i)
    {
        void* track = trackArray->entries[i];
        if (!track)
        {
            continue;
        }
        void* trial = *reinterpret_cast<void**>(static_cast<uint8_t*>(track) + 0x48);
        const uint32_t eventId = ReadEventIdFromGsr(trial);
        if (!eventId || eventId == kRejectedSentinelEvent)
        {
            continue;
        }
        if (!fallback)
        {
            fallback = track;
            fallbackIndex = i;
            fallbackEvent = eventId;
        }
        if (SpecialTrackHelpers::ContainsPopVirus(SpecialTrackHelpers::ReadTrackTitle(track)))
        {
            outIndex = i;
            outEventId = eventId;
            return track;
        }
    }

    outIndex = fallbackIndex;
    outEventId = fallbackEvent;
    return fallback;
}
}

namespace SpecialTrackInjection
{
void Reset()
{
    g_injected = false;
    DynamicTrackTitleSync::Reset();
    CustomJacketInstaller::Reset();
}

bool Inject(void* systemResource, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    if (g_injected || !systemResource)
    {
        return g_injected;
    }

    auto* trackArray = reinterpret_cast<RawArray*>(static_cast<uint8_t*>(systemResource) + 0x30);
    if (!trackArray->entries || trackArray->count == 0) return false;

    uint32_t sourceIndex = 0;
    uint32_t sourceEvent = 0;
    void* sourceTrack = PickSourceTrack(trackArray, sourceIndex, sourceEvent);
    if (!sourceTrack)
    {
        Log("music injection failed: no music-capable source track found");
        return false;
    }

    void* sourceText = *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceTrack) + 0x38);
    void* sourceAlbum = *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceTrack) + 0x30);
    void* sourceTrial = *reinterpret_cast<void**>(static_cast<uint8_t*>(sourceTrack) + 0x48);

    CloneChainResult chain;
    if (!BuildClonedChain(sourceTrial, chain))
    {
        Log("music injection failed: could not clone sound resource chain");
        return false;
    }

    void* newTrack = CloneTrack(sourceTrack, sourceText, chain);
    if (!newTrack)
    {
        Log("music injection failed: could not clone track");
        return false;
    }
    void* newAlbum = CloneAlbum(sourceAlbum, sourceText);
    if (newAlbum)
    {
        *reinterpret_cast<void**>(static_cast<uint8_t*>(newTrack) + 0x30) = newAlbum;
    }

    const uint32_t oldCount = trackArray->count;
    const uint32_t newCount = oldCount + 1;
    if (trackArray->capacity >= newCount)
    {
        trackArray->entries[oldCount] = newTrack;
        trackArray->count = newCount;
    }
    else
    {
        const uint32_t newCapacity = newCount + 8;
        auto** entries = static_cast<void**>(VirtualAlloc(nullptr,
            static_cast<size_t>(newCapacity) * sizeof(void*),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!entries)
        {
            Log("music injection failed: AllTracks allocation failed");
            return false;
        }
        memcpy(entries, trackArray->entries, static_cast<size_t>(oldCount) * sizeof(void*));
        entries[oldCount] = newTrack;
        trackArray->entries = entries;
        trackArray->count = newCount;
        trackArray->capacity = newCapacity;
    }

    std::ostringstream oss;
    oss << "injected special music track id=0x" << std::hex << SpecialTrackIds::kCustomTrackId
        << " event=0x" << SpecialTrackIds::kCustomEventId
        << " sourceIndex=" << std::dec << sourceIndex
        << " sourceTitle=\"" << SpecialTrackHelpers::ReadTrackTitle(sourceTrack) << "\""
        << " sourceEvent=0x" << std::hex << sourceEvent
        << " clonedEvent=0x" << chain.oldEventId
        << " AllTracks " << std::dec << oldCount << "->" << newCount;
    Log(oss.str());
    DynamicTrackTitleSync::Start(newTrack, newAlbum, logger);
    CustomJacketInstaller::TrackCreated(newTrack);
    g_injected = true;
    return true;
}
}
