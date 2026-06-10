#include "pch.h"

#include "SpecialTrackInjection.h"

#include "CustomJacketInstaller.h"
#include "DynamicTrackTitleSync.h"
#include "GameLayout.h"
#include "SpecialTrackIds.h"
#include "SpecialTrackHelpers.h"
#include "SpecialTrackSoundClone.h"

#include <sstream>

namespace
{
constexpr uint32_t kRejectedSentinelEvent = 82u;

constexpr size_t kTrackCloneSize = 0x300;
constexpr size_t kAlbumCloneSize = 0x80;

Logger* g_logger = nullptr;
bool g_injected = false;

void Log(const std::string& text)
{
    if (g_logger)
    {
        g_logger->Log(text);
    }
}

void* CloneTrack(void* sourceTrack, void* sourceText,
    SpecialTrackCloneChainResult& chain)
{
    auto* object = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(kTrackCloneSize));
    if (!object)
    {
        return nullptr;
    }

    memcpy(object, sourceTrack, kTrackCloneSize);
    SpecialTrackHelpers::ResetObjectHeader(object);
    *reinterpret_cast<uint32_t*>(object + GameLayout::Track::kId) =
        SpecialTrackIds::kCustomTrackId;
    *reinterpret_cast<uint16_t*>(object + GameLayout::Track::kDurationA) = 3600;
    *reinterpret_cast<int16_t*>(object + GameLayout::Track::kDurationB) = 30000;
    *reinterpret_cast<uint8_t*>(object + GameLayout::Track::kFlag) = 1;
    *reinterpret_cast<void**>(object + GameLayout::Track::kTitle) =
        SpecialTrackHelpers::CreateLocalizedText("!!! External Stream Source", sourceText);
    *reinterpret_cast<void**>(object + GameLayout::Track::kSoundA) = chain.gsr;
    *reinterpret_cast<void**>(object + GameLayout::Track::kSoundB) = chain.gsr;
    *reinterpret_cast<void**>(object + GameLayout::Track::kUnknown58) = nullptr;
    return object;
}

void* CloneAlbum(void* sourceAlbum, void* sourceText)
{
    if (!sourceAlbum) return nullptr;
    auto* object = static_cast<uint8_t*>(SpecialTrackHelpers::HeapAllocZero(kAlbumCloneSize));
    if (!object) return nullptr;
    memcpy(object, sourceAlbum, kAlbumCloneSize);
    SpecialTrackHelpers::ResetObjectHeader(object);
    void* artistText = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(sourceAlbum) + GameLayout::Album::kArtist);
    void* telopText = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(sourceAlbum) + GameLayout::Album::kTelopArtist);
    *reinterpret_cast<void**>(object + GameLayout::Album::kArtist) =
        SpecialTrackHelpers::CreateLocalizedText("", artistText ? artistText : sourceText);
    *reinterpret_cast<void**>(object + GameLayout::Album::kTelopArtist) =
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
        void* trial = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(track) + GameLayout::Track::kSoundB);
        const uint32_t eventId = SpecialTrackSoundClone::ReadEventIdFromGsr(trial);
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

    void* sourceText = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(sourceTrack) + GameLayout::Track::kTitle);
    void* sourceAlbum = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(sourceTrack) + GameLayout::Track::kAlbum);
    void* sourceTrial = *reinterpret_cast<void**>(
        static_cast<uint8_t*>(sourceTrack) + GameLayout::Track::kSoundB);

    SpecialTrackCloneChainResult chain;
    if (!SpecialTrackSoundClone::Build(sourceTrial, chain))
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
        *reinterpret_cast<void**>(
            static_cast<uint8_t*>(newTrack) + GameLayout::Track::kAlbum) = newAlbum;
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
