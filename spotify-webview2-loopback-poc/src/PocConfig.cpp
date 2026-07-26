#include "PocConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace
{
std::string LoadJsonString(
    const std::filesystem::path& configPath,
    const std::string& propertyName)
{
    std::ifstream input(configPath, std::ios::binary);
    if (!input)
    {
        return {};
    }
    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::string key = '"' + propertyName + '"';
    const std::string::size_type keyPosition = contents.find(key);
    if (keyPosition == std::string::npos)
    {
        return {};
    }
    const std::string::size_type colonPosition =
        contents.find(':', keyPosition + key.size());
    if (colonPosition == std::string::npos)
    {
        return {};
    }
    const std::string::size_type quotePosition =
        contents.find('"', colonPosition + 1);
    if (quotePosition == std::string::npos)
    {
        return {};
    }
    const std::string::size_type endQuotePosition =
        contents.find('"', quotePosition + 1);
    if (endQuotePosition == std::string::npos)
    {
        return {};
    }
    return contents.substr(
        quotePosition + 1,
        endQuotePosition - quotePosition - 1);
}

bool IsSpotifyClientId(const std::string& value)
{
    return value.size() == 32 &&
        std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isxdigit(character) != 0;
            });
}

bool IsProxyServer(const std::string& value)
{
    const bool supportedScheme =
        value.starts_with("http://") ||
        value.starts_with("https://") ||
        value.starts_with("socks://") ||
        value.starts_with("socks5://");
    return supportedScheme &&
        value.size() <= 256 &&
        std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isalnum(character) != 0 ||
                    std::string(".-_:/[]").find(
                        static_cast<char>(character)) != std::string::npos;
            });
}

bool LoadJsonBool(
    const std::filesystem::path& configPath,
    const std::string& propertyName)
{
    std::ifstream input(configPath, std::ios::binary);
    if (!input)
    {
        return false;
    }
    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::string key = '"' + propertyName + '"';
    const auto keyPosition = contents.find(key);
    const auto colonPosition = keyPosition == std::string::npos
        ? std::string::npos
        : contents.find(':', keyPosition + key.size());
    if (colonPosition == std::string::npos)
    {
        return false;
    }
    const auto valuePosition =
        contents.find_first_not_of(" \t\r\n", colonPosition + 1);
    return valuePosition != std::string::npos &&
        contents.compare(valuePosition, 4, "true") == 0;
}
}

std::wstring LoadSpotifyClientId(
    const std::filesystem::path& configPath)
{
    const std::string clientId =
        LoadJsonString(configPath, "spotifyClientId");
    if (!IsSpotifyClientId(clientId))
    {
        return {};
    }
    return std::wstring(clientId.begin(), clientId.end());
}

std::wstring LoadProxyServer(
    const std::filesystem::path& configPath)
{
    const std::string proxyServer =
        LoadJsonString(configPath, "proxyServer");
    if (!IsProxyServer(proxyServer))
    {
        return {};
    }
    return std::wstring(proxyServer.begin(), proxyServer.end());
}

bool LoadDiagnosticsEnabled(
    const std::filesystem::path& configPath)
{
    return LoadJsonBool(configPath, "diagnostics");
}
