#pragma once

#include <atomic>

namespace RuntimeFeatureState
{
inline std::atomic<bool>& SourceAudioReady()
{
    static std::atomic<bool> ready{false};
    return ready;
}
}
