#include "pch.h"

#include "LocalizedTrackText.h"

#include "SpecialTrackHelpers.h"

#include <cstdint>
#include <cstring>

namespace
{
constexpr size_t kTextBytes = 1024;
constexpr uint32_t kTrackTitleOffset = 0x38;
constexpr uint32_t kAlbumArtistOffset = 0x30;
constexpr uint32_t kAlbumTelopArtistOffset = 0x40;

bool ResolveTextObject(void* owner, uint32_t offset, void*& textObject)
{
    __try
    {
        if (!owner)
        {
            return false;
        }

        textObject = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(owner) + offset);
        return textObject != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool BindSlot(void* owner, uint32_t offset, LocalizedTrackText::TextSlot& slot)
{
    void* textObject = nullptr;
    if (!ResolveTextObject(owner, offset, textObject))
    {
        return false;
    }
    if (slot.text != textObject)
    {
        slot.text = textObject;
        slot.buffer = nullptr;
    }
    return true;
}

const char* ReadTextValue(void* textObject)
{
    __try
    {
        if (!textObject)
        {
            return "";
        }

        const auto* textBytes = static_cast<uint8_t*>(textObject);
        const char* value =
            *reinterpret_cast<const char* const*>(textBytes + 0x20);
        return value ? value : "";
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "";
    }
}

bool TextMatches(const LocalizedTrackText::TextSlot& slot, const char* value)
{
    return strcmp(ReadTextValue(slot.text), value ? value : "") == 0;
}

bool EnsureMutableText(LocalizedTrackText::TextSlot& slot)
{
    __try
    {
        if (!slot.text)
        {
            return false;
        }
        if (!slot.buffer)
        {
            slot.buffer = static_cast<char*>(
                SpecialTrackHelpers::HeapAllocZero(kTextBytes));
            if (!slot.buffer)
            {
                return false;
            }
        }

        auto* textBytes = static_cast<uint8_t*>(slot.text);
        char* oldText = *reinterpret_cast<char**>(textBytes + 0x20);
        if (oldText == slot.buffer)
        {
            return true;
        }
        if (oldText && oldText[0])
        {
            strcpy_s(slot.buffer, kTextBytes, oldText);
        }

        *reinterpret_cast<char**>(textBytes + 0x20) = slot.buffer;
        const size_t length = strlen(slot.buffer);
        *reinterpret_cast<uint16_t*>(textBytes + 0x28) =
            static_cast<uint16_t>(length);
        *reinterpret_cast<int16_t*>(textBytes + 0x2A) = 0;
        if (oldText)
        {
            HeapFree(GetProcessHeap(), 0, oldText);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool WriteMutableText(LocalizedTrackText::TextSlot& slot, const char* value)
{
    __try
    {
        if (!slot.text || !slot.buffer)
        {
            return false;
        }

        strcpy_s(slot.buffer, kTextBytes, value ? value : "");
        const size_t length = strlen(slot.buffer);
        auto* textBytes = static_cast<uint8_t*>(slot.text);
        *reinterpret_cast<uint16_t*>(textBytes + 0x28) =
            static_cast<uint16_t>(length);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SetText(LocalizedTrackText::TextSlot& slot, const char* value)
{
    return EnsureMutableText(slot) && WriteMutableText(slot, value);
}

void ResetSlot(LocalizedTrackText::TextSlot& slot)
{
    slot.text = nullptr;
    slot.buffer = nullptr;
}
}

namespace LocalizedTrackText
{
void Reset(State& state)
{
    ResetSlot(state.title);
    ResetSlot(state.artist);
    ResetSlot(state.telopArtist);
    state.lastTitle.clear();
    state.lastArtist.clear();
}

bool Resolve(State& state, void* track, void* album)
{
    if (!BindSlot(track, kTrackTitleOffset, state.title))
    {
        return false;
    }

    if (album)
    {
        BindSlot(album, kAlbumArtistOffset, state.artist);
        BindSlot(album, kAlbumTelopArtistOffset, state.telopArtist);
    }

    return true;
}

UpdateResult Apply(State& state, const char* title, const char* artist)
{
    UpdateResult result;
    result.titleText = state.title.text;
    result.artistText = state.artist.text;

    if (title && strcmp(title, state.lastTitle.c_str()) != 0)
    {
        if (TextMatches(state.title, title) || SetText(state.title, title))
        {
            state.lastTitle = title;
            result.changed = true;
        }
    }

    if (artist && strcmp(artist, state.lastArtist.c_str()) != 0)
    {
        const bool artistCurrent = TextMatches(state.artist, artist);
        const bool telopCurrent = !state.telopArtist.text ||
            TextMatches(state.telopArtist, artist);
        if (artistCurrent && telopCurrent)
        {
            state.lastArtist = artist;
            result.changed = true;
        }
        else if (SetText(state.artist, artist))
        {
            if (state.telopArtist.text &&
                state.telopArtist.text != state.artist.text)
            {
                SetText(state.telopArtist, artist);
            }

            state.lastArtist = artist;
            result.changed = true;
        }
    }

    return result;
}
}
