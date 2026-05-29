#include "pch.h"

#include "SourceAudioBootstrap.h"

#include "RuntimeFeatureState.h"
#include "SourcePluginBank.h"
#include "WwisePluginRegistration.h"

#include <mutex>

namespace
{
constexpr DWORD kRetryDelayMs = 250;

std::mutex g_mutex;
HMODULE g_gameModule = nullptr;
HMODULE g_selfModule = nullptr;

bool TryOnce(const Logger& logger)
{
    if (!g_gameModule || !g_selfModule)
    {
        logger.Log("source audio bootstrap missing module handles");
        return false;
    }

    if (!WwisePluginRegistration::TryRegister(g_gameModule, g_selfModule, logger))
    {
        logger.Log("source audio bootstrap register attempt failed");
        return false;
    }

    const int32_t bankResult = SourcePluginBank::TryLoad(g_gameModule, logger);
    if (bankResult != 1)
    {
        logger.Log("source audio bootstrap bank attempt failed");
        return false;
    }

    RuntimeFeatureState::SourceAudioReady().store(true);
    return true;
}
}

namespace SourceAudioBootstrap
{
void Configure(HMODULE gameModule, HMODULE selfModule)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_gameModule = gameModule;
    g_selfModule = selfModule;
}

bool EnsureReady(const Logger& logger, DWORD timeoutMs)
{
    if (RuntimeFeatureState::SourceAudioReady().load())
    {
        return true;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    const DWORD start = GetTickCount();
    do
    {
        if (TryOnce(logger))
        {
            logger.Log("source audio bootstrap ready");
            return true;
        }

        Sleep(kRetryDelayMs);
    } while (GetTickCount() - start < timeoutMs);

    logger.Log("source audio bootstrap timed out");
    return false;
}
}
