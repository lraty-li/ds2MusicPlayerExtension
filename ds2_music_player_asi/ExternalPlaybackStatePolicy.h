#pragma once

#include <cstdint>

namespace ExternalPlaybackStatePolicy
{
enum class Decision : uint8_t
{
    MarkApplied,
    Defer,
    Pause,
    Resume,
};

struct Input
{
    bool customTrack;
    bool known;
    bool paused;
    uint8_t playState;
    bool hasCurrentRuntime;
    uint16_t autoBlockMask;
};

constexpr Decision Decide(const Input& input)
{
    if (!input.customTrack || !input.known)
    {
        return Decision::MarkApplied;
    }
    if (input.autoBlockMask || input.playState == 3 || input.playState == 4)
    {
        return Decision::MarkApplied;
    }
    if (input.playState == 5)
    {
        return input.paused ? Decision::Defer : Decision::MarkApplied;
    }
    if (input.playState == 1 && input.paused)
    {
        return input.hasCurrentRuntime ? Decision::Pause : Decision::Defer;
    }
    if (input.playState == 2 && !input.paused)
    {
        return input.hasCurrentRuntime ? Decision::Resume : Decision::Defer;
    }
    return Decision::MarkApplied;
}

static_assert(Decide({true, true, true, 1, true, 0}) == Decision::Pause);
static_assert(Decide({true, true, false, 2, true, 0}) == Decision::Resume);
static_assert(Decide({true, true, false, 1, true, 0}) == Decision::MarkApplied);
static_assert(Decide({true, true, true, 2, true, 0}) == Decision::MarkApplied);
static_assert(Decide({true, true, true, 3, true, 1}) == Decision::MarkApplied);
static_assert(Decide({true, true, false, 4, true, 0}) == Decision::MarkApplied);
static_assert(Decide({true, true, true, 5, true, 0}) == Decision::Defer);
static_assert(Decide({true, true, true, 1, false, 0}) == Decision::Defer);
static_assert(Decide({false, true, true, 1, true, 0}) == Decision::MarkApplied);
static_assert(Decide({true, false, true, 1, true, 0}) == Decision::MarkApplied);
}
