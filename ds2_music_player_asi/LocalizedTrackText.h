#pragma once

#include <string>

namespace LocalizedTrackText
{
struct State
{
    void* titleText = nullptr;
    void* artistText = nullptr;
    void* telopArtistText = nullptr;
    char* titleBuffer = nullptr;
    char* artistBuffer = nullptr;
    char* telopArtistBuffer = nullptr;
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
