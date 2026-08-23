#pragma once

#include <cstdint>

namespace GameLayout
{
namespace Track
{
constexpr uint32_t kId = 0x20;
constexpr uint32_t kDurationA = 0x24;
constexpr uint32_t kDurationB = 0x26;
constexpr uint32_t kFlag = 0x28;
constexpr uint32_t kAlbum = 0x30;
constexpr uint32_t kTitle = 0x38;
constexpr uint32_t kSoundA = 0x40;
constexpr uint32_t kSoundB = 0x48;
constexpr uint32_t kJacket = 0x50;
constexpr uint32_t kUnknown58 = 0x58;
}

namespace Album
{
constexpr uint32_t kArtist = 0x30;
constexpr uint32_t kTelopArtist = 0x40;
}

namespace LocalizedText
{
constexpr uint32_t kText = 0x20;
constexpr uint32_t kLength = 0x28;
constexpr uint32_t kFlags = 0x2A;
}

namespace MusicRuntime
{
constexpr uint32_t kPlayState = 0x1910;
constexpr uint32_t kAutoBlockMask = 0x1912;
constexpr uint32_t kCurrentRuntime = 0x1918;
constexpr uint32_t kCurrentTrackId = 0x1924;
constexpr uint32_t kEntryCount = 0x1938;
constexpr uint32_t kEntryData = 0x1940;
}

namespace MusicEntry
{
constexpr uint32_t kTitle = 0x00;
constexpr uint32_t kArtist = 0x08;
constexpr uint32_t kTrack = 0x10;
constexpr uint32_t kAlbum = 0x18;
constexpr uint32_t kTrackField24 = 0x28;
constexpr uint32_t kFlag = 0x34;
constexpr uint32_t kStride = 0x38;
}

namespace StreamingTarget
{
constexpr uint32_t kLoaded = 0x20;
constexpr uint32_t kRefCount = 0x28;
constexpr uint32_t kSize = 0x30;
}

namespace UiTexture
{
constexpr uint32_t kTexture = 0x30;
constexpr uint32_t kSelf = 0x38;
}

namespace Texture
{
constexpr uint32_t kRefCount = 0x08;
constexpr uint32_t kTextureDx12 = 0x20;
constexpr uint32_t kChain0 = 0x70;
constexpr uint32_t kChain1 = 0xE0;
constexpr uint32_t kChain2 = 0x150;
constexpr uint32_t kChain3 = 0x1C0;
}

namespace TextureDx12
{
constexpr uint32_t kMainWrapper = 0x88;
constexpr uint32_t kMainView = 0x90;
constexpr uint32_t kSecondaryWrapper = 0xD8;
constexpr uint32_t kSecondaryView = 0xE0;
}

namespace ResourceWrapper
{
constexpr uint32_t kD3D12Resource = 0x08;
}

namespace CatalogueResource
{
constexpr uint32_t kDefaultConstructionHoloImageTexture = 0xC8;
}
} // namespace GameLayout
