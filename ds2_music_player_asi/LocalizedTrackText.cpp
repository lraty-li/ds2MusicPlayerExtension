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

bool TextMatches(void* textObject, const char* value)
{
    return strcmp(ReadTextValue(textObject), value ? value : "") == 0;
}

bool EnsureMutableText(void* textObject, char*& buffer)
{
    __try
    {
        if (!textObject)
        {
            return false;
        }
        if (!buffer)
        {
            buffer = static_cast<char*>(
                SpecialTrackHelpers::HeapAllocZero(kTextBytes));
            if (!buffer)
            {
                return false;
            }
        }

        auto* textBytes = static_cast<uint8_t*>(textObject);
        char* oldText = *reinterpret_cast<char**>(textBytes + 0x20);
        if (oldText == buffer)
        {
            return true;
        }
        if (oldText && oldText[0])
        {
            strcpy_s(buffer, kTextBytes, oldText);
        }

        *reinterpret_cast<char**>(textBytes + 0x20) = buffer;
        const size_t length = strlen(buffer);
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

bool WriteMutableText(void* textObject, char* buffer, const char* value)
{
    __try
    {
        if (!textObject || !buffer)
        {
            return false;
        }

        strcpy_s(buffer, kTextBytes, value ? value : "");
        const size_t length = strlen(buffer);
        auto* textBytes = static_cast<uint8_t*>(textObject);
        *reinterpret_cast<uint16_t*>(textBytes + 0x28) =
            static_cast<uint16_t>(length);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SetText(void* textObject, char*& buffer, const char* value)
{
    return EnsureMutableText(textObject, buffer) &&
        WriteMutableText(textObject, buffer, value);
}
}

namespace LocalizedTrackText
{
void Reset(State& state)
{
    state.titleText = nullptr;
    state.artistText = nullptr;
    state.telopArtistText = nullptr;
    state.lastTitle.clear();
    state.lastArtist.clear();
}

bool Resolve(State& state, void* track, void* album)
{
    if (!state.titleText &&
        !ResolveTextObject(track, kTrackTitleOffset, state.titleText))
    {
        return false;
    }

    if (album)
    {
        if (!state.artistText)
        {
            ResolveTextObject(album, kAlbumArtistOffset, state.artistText);
        }
        if (!state.telopArtistText)
        {
            ResolveTextObject(
                album, kAlbumTelopArtistOffset, state.telopArtistText);
        }
    }

    return true;
}

UpdateResult Apply(State& state, const char* title, const char* artist)
{
    UpdateResult result;
    result.titleText = state.titleText;
    result.artistText = state.artistText;

    if (title && strcmp(title, state.lastTitle.c_str()) != 0)
    {
        if (TextMatches(state.titleText, title) ||
            SetText(state.titleText, state.titleBuffer, title))
        {
            state.lastTitle = title;
            result.changed = true;
        }
    }

    if (artist && strcmp(artist, state.lastArtist.c_str()) != 0)
    {
        const bool artistCurrent = TextMatches(state.artistText, artist);
        const bool telopCurrent = !state.telopArtistText ||
            TextMatches(state.telopArtistText, artist);
        if (artistCurrent && telopCurrent)
        {
            state.lastArtist = artist;
            result.changed = true;
        }
        else if (SetText(state.artistText, state.artistBuffer, artist))
        {
            if (state.telopArtistText &&
                state.telopArtistText != state.artistText)
            {
                SetText(
                    state.telopArtistText, state.telopArtistBuffer, artist);
            }

            state.lastArtist = artist;
            result.changed = true;
        }
    }

    return result;
}
}
