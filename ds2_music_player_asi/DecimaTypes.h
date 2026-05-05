#pragma once

#include <cstdint>

struct RawArray
{
    uint32_t count;
    uint32_t capacity;
    void** entries;
};
