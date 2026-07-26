#include "PocConfig.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace
{
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
}

std::wstring LoadSpotifyClientId(
    const std::filesystem::path& configPath)
{
    std::ifstream input(configPath, std::ios::binary);
    if (!input)
    {
        return {};
    }

    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    constexpr char key[] = "\"spotifyClientId\"";
    const std::string::size_type keyPosition = contents.find(key);
    if (keyPosition == std::string::npos)
    {
        return {};
    }

    const std::string::size_type colonPosition =
        contents.find(':', keyPosition + sizeof(key) - 1);
    const std::string::size_type quotePosition =
        contents.find('"', colonPosition + 1);
    const std::string::size_type endQuotePosition =
        contents.find('"', quotePosition + 1);
    if (colonPosition == std::string::npos ||
        quotePosition == std::string::npos ||
        endQuotePosition == std::string::npos)
    {
        return {};
    }

    const std::string clientId = contents.substr(
        quotePosition + 1,
        endQuotePosition - quotePosition - 1);
    if (!IsSpotifyClientId(clientId))
    {
        return {};
    }
    return std::wstring(clientId.begin(), clientId.end());
}
