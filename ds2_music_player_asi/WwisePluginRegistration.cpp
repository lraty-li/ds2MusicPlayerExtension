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

    const int result = symbols.registerPluginDll(kPluginName, pluginDir.c_str());

    std::ostringstream oss;
    oss << "RegisterPluginDLL result=" << result;
    logger.Log(oss.str());
    return result == 1;
}
}
