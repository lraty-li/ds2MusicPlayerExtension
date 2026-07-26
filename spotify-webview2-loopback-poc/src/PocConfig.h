#pragma once

#include <filesystem>
#include <string>

std::wstring LoadSpotifyClientId(
    const std::filesystem::path& configPath);
std::wstring LoadProxyServer(
    const std::filesystem::path& configPath);
