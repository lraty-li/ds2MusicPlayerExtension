#pragma once

#include <filesystem>
#include <string>

std::wstring LoadSpotifyClientId(
    const std::filesystem::path& configPath);
