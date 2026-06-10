#pragma once

#include <string>

namespace LocalizedTrackText
{
struct TextSlot
{
    void* text = nullptr;
    char* buffer = nullptr;
};

struct State
{
    TextSlot title;
    TextSlot artist;
    TextSlot telopArtist;
    std::string lastTitle;
    std::string lastArtist;
};

struct UpdateResult
{
    bool changed = false;
    void* titleText = nullptr;
    void* artistText = nullptr;
};

void Reset(State& state);
bool Resolve(State& state, void* track, void* album);
UpdateResult Apply(State& state, const char* title, const char* artist);
}
