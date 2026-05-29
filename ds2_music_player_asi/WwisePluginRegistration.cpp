#include "pch.h"

#include "WwisePluginRegistration.h"

#include "GameSymbols.h"
#include "HookUtils.h"

#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kPluginName[] = L"ds2_dll_music_resource";
constexpr wchar_t kPluginDllName[] = L"ds2_dll_music_resource.dll";

struct SehFailure
{
    DWORD code = 0;
    void* address = nullptr;
};

struct PluginListInfo
{
    void* entry = nullptr;
    uint32_t type = 0;
    uint32_t company = 0;
    uint32_t plugin = 0;
    void* create = nullptr;
    void* params = nullptr;
};

using RegisterPluginListFn = int(__fastcall*)(void*);

int CaptureSeh(EXCEPTION_POINTERS* info, SehFailure* failure)
{
    if (info && info->ExceptionRecord && failure)
    {
        failure->code = info->ExceptionRecord->ExceptionCode;
        failure->address = info->ExceptionRecord->ExceptionAddress;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

bool TryReadPluginList(void* pluginListExport, PluginListInfo* info)
{
    __try
    {
        auto* entry = *reinterpret_cast<uint8_t**>(pluginListExport);
        info->entry = entry;
        info->type = *(entry + 0x08);
        info->company = *reinterpret_cast<uint32_t*>(entry + 0x0C);
        info->plugin = *reinterpret_cast<uint32_t*>(entry + 0x10);
        info->create = *reinterpret_cast<void**>(entry + 0x18);
        info->params = *reinterpret_cast<void**>(entry + 0x20);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

RegisterPluginListFn ResolveRegisterPluginList(
    HMODULE gameModule, GameSymbols::RegisterPluginDllFn wrapper, const Logger& logger)
{
    auto* bytes = reinterpret_cast<const uint8_t*>(wrapper);
    for (size_t i = 0; i + 8 < 0x120; ++i)
    {
        if (bytes[i] != 0x48 || bytes[i + 1] != 0x8B ||
            bytes[i + 2] != 0x08 || bytes[i + 3] != 0xE8)
        {
            continue;
        }

        int32_t rel = 0;
        memcpy(&rel, bytes + i + 4, sizeof(rel));
        const uintptr_t callEnd = reinterpret_cast<uintptr_t>(bytes + i + 8);
        const uintptr_t target = callEnd + rel;
        if (!HookUtils::IsAddressRangeInModule(gameModule, target, 1))
        {
            continue;
        }

        std::ostringstream oss;
        oss << "RegisterPluginList resolved from wrapper rva="
            << HookUtils::HexU64(target - reinterpret_cast<uintptr_t>(gameModule))
            << " address=" << HookUtils::HexU64(target);
        logger.Log(oss.str());
        return reinterpret_cast<RegisterPluginListFn>(target);
    }

    logger.Log("RegisterPluginList fallback unresolved");
    return nullptr;
}

bool SafeRegisterPluginDll(GameSymbols::RegisterPluginDllFn registerPluginDll,
    const wchar_t* name, const wchar_t* dir, int* result, SehFailure* failure)
{
    __try
    {
        *result = registerPluginDll(name, dir);
        return true;
    }
    __except (CaptureSeh(GetExceptionInformation(), failure))
    {
        return false;
    }
}

bool SafeRegisterPluginList(RegisterPluginListFn registerPluginList,
    void* entry, int* result, SehFailure* failure)
{
    __try
    {
        *result = registerPluginList(entry);
        return true;
    }
    __except (CaptureSeh(GetExceptionInformation(), failure))
    {
        return false;
    }
}

std::wstring ParentDirectory(std::wstring path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return {};
    }

    path.resize(pos);
    return path;
}

std::wstring JoinPath(const std::wstring& dir, const wchar_t* file)
{
    if (dir.empty())
    {
        return file;
    }

    std::wstring result = dir;
    const wchar_t last = result.back();
    if (last != L'\\' && last != L'/')
    {
        result += L'\\';
    }
    result += file;
    return result;
}

void LogPluginList(void* pluginListExport, const Logger& logger)
{
    PluginListInfo info;
    if (TryReadPluginList(pluginListExport, &info))
    {
        std::ostringstream oss;
        oss << "g_pAKPluginList export=" << pluginListExport
            << " entry=" << info.entry
            << " type=" << info.type
            << " company=" << info.company
            << " plugin=" << info.plugin
            << " create=" << info.create
            << " params=" << info.params;
        logger.Log(oss.str());
        return;
    }
    logger.Log("g_pAKPluginList probe failed");
}
}

namespace WwisePluginRegistration
{
bool TryRegister(HMODULE gameModule, HMODULE selfModule, const Logger& logger)
{
    if (!gameModule || !selfModule)
    {
        logger.Log("stream plugin register skipped: missing module handle");
        return false;
    }

    const std::wstring selfPath = HookUtils::GetModulePath(selfModule);
    const std::wstring pluginDir = ParentDirectory(selfPath);
    const std::wstring pluginPath = JoinPath(pluginDir, kPluginDllName);
    if (pluginDir.empty())
    {
        logger.Log("stream plugin register skipped: cannot resolve ASI directory");
        return false;
    }

    HMODULE pluginModule = GetModuleHandleW(pluginPath.c_str());
    if (!pluginModule)
    {
        pluginModule = LoadLibraryW(pluginPath.c_str());
    }
    if (!pluginModule)
    {
        logger.Log("stream plugin LoadLibrary failed path=" +
            HookUtils::NarrowUtf8(pluginPath) +
            " err=" + std::to_string(GetLastError()));
        return false;
    }

    void* pluginList = GetProcAddress(pluginModule, "g_pAKPluginList");
    {
        std::ostringstream oss;
        oss << "stream plugin loaded module=" << pluginModule
            << " g_pAKPluginList=" << pluginList;
        logger.Log(oss.str());
    }
    if (!pluginList)
    {
        logger.Log("stream plugin skipped: g_pAKPluginList export missing");
        return false;
    }
    LogPluginList(pluginList, logger);

    const GameSymbols::ResolvedSymbols symbols = GameSymbols::Resolve(gameModule, logger);
    if (!symbols.registerPluginDll)
    {
        logger.Log("stream plugin register skipped: RegisterPluginDLL unresolved");
        return false;
    }

    {
        std::ostringstream oss;
        oss << "RegisterPluginDLL pluginName=" << HookUtils::NarrowUtf8(kPluginName)
            << " basePath=" << HookUtils::NarrowUtf8(pluginDir);
        logger.Log(oss.str());
    }

    int result = 0;
    SehFailure seh;
    if (!SafeRegisterPluginDll(symbols.registerPluginDll, kPluginName,
        pluginDir.c_str(), &result, &seh))
    {
        std::ostringstream crash;
        crash << "RegisterPluginDLL exception code=" << HookUtils::HexU64(seh.code)
            << " address=" << seh.address;
        logger.Log(crash.str());

        PluginListInfo info;
        auto registerPluginList =
            ResolveRegisterPluginList(gameModule, symbols.registerPluginDll, logger);
        if (!registerPluginList || !TryReadPluginList(pluginList, &info) || !info.entry)
        {
            return false;
        }

        SehFailure listSeh;
        int listResult = 0;
        if (!SafeRegisterPluginList(registerPluginList, info.entry,
            &listResult, &listSeh))
        {
            std::ostringstream listCrash;
            listCrash << "RegisterPluginList exception code="
                << HookUtils::HexU64(listSeh.code)
                << " address=" << listSeh.address;
            logger.Log(listCrash.str());
            return false;
        }

        std::ostringstream listLog;
        listLog << "RegisterPluginList fallback result=" << listResult;
        logger.Log(listLog.str());
        return listResult == 1;
    }

    std::ostringstream oss;
    oss << "RegisterPluginDLL result=" << result;
    logger.Log(oss.str());
    return result == 1;
}
}
