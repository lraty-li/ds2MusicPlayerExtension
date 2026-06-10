#include "pch.h"

#include "MusicPlayerInjection.h"

#include "CustomJacketInstaller.h"
#include "DecimaTypes.h"
#include "DynamicTrackTitleSync.h"
#include "FailFast.h"
#include "PatternScan.h"
#include "RuntimeFeatureState.h"
#include "SourceAudioBootstrap.h"
#include "SpecialTrackInjection.h"

#include <sstream>

namespace
{
const char* kStreamingInstancePattern =
    "48 89 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2 "
    "41 B8 F8 0A 00 00 48 8B C8 48 8B D8 E8";

Logger* g_logger = nullptr;

void Log(const std::string& text)
{
    if (g_logger)
    {
        g_logger->Log(text);
    }
}

const char* RttiTypeName(void* object)
{
    __try
    {
        if (!object)
        {
            return nullptr;
        }

        auto** vtable = *reinterpret_cast<void***>(object);
        using GetRttiFn = void* (__fastcall*)(void*);
        void* rtti = reinterpret_cast<GetRttiFn>(vtable[0])(object);
        if (!rtti || *(static_cast<uint8_t*>(rtti) + 4) != 4)
        {
            return nullptr;
        }
        return *reinterpret_cast<const char**>(static_cast<uint8_t*>(rtti) + 0x40);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

class MusicEventListener
{
public:
    virtual void OnFinishLoadGroup(const RawArray* objects)
    {
        if (!objects || !objects->entries)
        {
            return;
        }

        for (uint32_t i = 0; i < objects->count; ++i)
        {
            void* object = objects->entries[i];
            const char* typeName = RttiTypeName(object);
            if (typeName && strcmp(typeName, "DSMusicPlayerSystemResource") == 0)
            {
                Log("OnFinishLoadGroup: DSMusicPlayerSystemResource found");
                if (!RuntimeFeatureState::SourceAudioReady().load())
                {
                    Log("music resource loaded before source audio bank; bootstrapping now");
                    if (!SourceAudioBootstrap::EnsureReady(*g_logger, 15000))
                    {
                        FailFast::Now(*g_logger, "source audio bootstrap failed");
                    }
                }
                if (!SpecialTrackInjection::Inject(object, *g_logger))
                {
                    FailFast::Now(*g_logger, "special track injection failed");
                }
            }
            else if (typeName && strcmp(typeName, "DSUICatalogueImageResource") == 0)
            {
                Log("OnFinishLoadGroup: DSUICatalogueImageResource found");
                CustomJacketInstaller::TryApply(object, *g_logger);
            }
        }
        DynamicTrackTitleSync::ApplyPendingOnGameThread();
    }

    virtual void OnBeforeUnloadGroup(const RawArray* objects)
    {
        DynamicTrackTitleSync::ApplyPendingOnGameThread();
        if (!objects || !objects->entries)
        {
            return;
        }

        for (uint32_t i = 0; i < objects->count; ++i)
        {
            const char* typeName = RttiTypeName(objects->entries[i]);
            if (typeName && strcmp(typeName, "DSMusicPlayerSystemResource") == 0)
            {
                SpecialTrackInjection::Reset();
                CustomJacketInstaller::Reset();
                Log("DSMusicPlayerSystemResource unloading; injection reset");
            }
        }
    }

    virtual void OnLoadAssetGroup(const RawArray*)
    {
        DynamicTrackTitleSync::ApplyPendingOnGameThread();
    }
};

MusicEventListener g_listener;

bool WaitForStreamingManager(void** managerSlot, void*& manager)
{
    manager = nullptr;
    for (int i = 0; i < 600 && !manager; ++i)
    {
        __try
        {
            manager = *managerSlot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            manager = nullptr;
        }

        if (!manager)
        {
            Sleep(100);
        }
    }
    return manager != nullptr;
}
}

namespace MusicPlayerInjection
{
bool TryInstall(HMODULE gameModule, const Logger& logger)
{
    g_logger = const_cast<Logger*>(&logger);
    if (!gameModule)
    {
        Log("music listener skipped: missing game module");
        return false;
    }

    uintptr_t textStart = 0;
    size_t textSize = 0;
    if (!PatternScan::GetSection(gameModule, ".text", textStart, textSize))
    {
        Log("music listener skipped: .text not found");
        return false;
    }

    uintptr_t signature = PatternScan::Find(textStart, textSize, kStreamingInstancePattern);
    if (!signature)
    {
        Log("music listener skipped: StreamingManager pattern not found");
        return false;
    }

    auto** managerSlot = reinterpret_cast<void**>(PatternScan::ResolveRip(signature, 3));
    std::ostringstream found;
    found << "StreamingManager global slot=" << managerSlot;
    Log(found.str());

    void* manager = nullptr;
    if (!WaitForStreamingManager(managerSlot, manager))
    {
        Log("music listener skipped: StreamingManager not ready");
        return false;
    }

    void* streamingSystem = *reinterpret_cast<void**>(static_cast<uint8_t*>(manager) + 0x578);
    if (!streamingSystem)
    {
        Log("music listener skipped: mStreamingSystem is null");
        return false;
    }

    auto** vtable = *reinterpret_cast<void***>(streamingSystem);
    using AddListenerFn = void (__fastcall*)(void*, void*);
    reinterpret_cast<AddListenerFn>(vtable[3])(streamingSystem, &g_listener);

    std::ostringstream registered;
    registered << "music listener registered: manager=" << manager
        << " streamSystem=" << streamingSystem;
    Log(registered.str());
    return true;
}
}
