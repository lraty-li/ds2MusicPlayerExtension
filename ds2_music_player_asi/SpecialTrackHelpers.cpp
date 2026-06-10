#include "pch.h"

#include "SpecialTrackHelpers.h"

#include "GameLayout.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
char* CloneCString(const char* text)
{
    const size_t length = strlen(text);
    auto* result = static_cast<char*>(SpecialTrackHelpers::HeapAllocZero(length + 1));
    if (result)
    {
        memcpy(result, text, length);
    }
    return result;
}
} // namespace

namespace SpecialTrackHelpers
{
void* HeapAllocZero(size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void ResetObjectHeader(void* object)
{
    auto* bytes = static_cast<uint8_t*>(object);
    *reinterpret_cast<uint32_t*>(bytes + 0x08) = 1;
    for (int i = 0; i < 16; ++i)
    {
        bytes[0x10 + i] = static_cast<uint8_t>(GetTickCount() + i * 17);
    }
}

void* CreateLocalizedText(const char* text, void* sourceText)
{
    auto* object = static_cast<uint8_t*>(HeapAllocZero(0x40));
    if (!object || !sourceText)
    {
        return nullptr;
    }

    *reinterpret_cast<void**>(object) = *reinterpret_cast<void**>(sourceText);
    ResetObjectHeader(object);
    *reinterpret_cast<const char**>(object + GameLayout::LocalizedText::kText) =
        CloneCString(text);
    *reinterpret_cast<uint16_t*>(object + GameLayout::LocalizedText::kLength) =
        static_cast<uint16_t>(strlen(text));
    *reinterpret_cast<int16_t*>(object + GameLayout::LocalizedText::kFlags) = 0;
    *reinterpret_cast<void**>(object + 0x30) = nullptr;
    return object;
}

const char* ReadTrackTitle(void* track)
{
    __try
    {
        void* text = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(track) + GameLayout::Track::kTitle);
        if (!text)
        {
            return "";
        }
        const char* value = *reinterpret_cast<const char**>(
            static_cast<uint8_t*>(text) + GameLayout::LocalizedText::kText);
        return value ? value : "";
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return "";
    }
}

bool ContainsPopVirus(const char* title)
{
    std::string lower = title ? title : "";
    for (char& c : lower)
    {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("pop virus") != std::string::npos;
}
} // namespace SpecialTrackHelpers
