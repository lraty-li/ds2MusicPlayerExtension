#include "pch.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace
{
constexpr uint8_t kPluginType = 2;
constexpr uint32_t kCompanyId = 0x6A7;
constexpr uint32_t kPluginId = 0x101;

std::mutex g_logMutex;

std::wstring GetLogPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    std::wstring result = path;
    const size_t pos = result.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        result.resize(pos + 1);
    }

    result += L"ds2_dll_music_resource.log";
    return result;
}

void Log(const char* text)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    HANDLE file = CreateFileW(
        GetLogPath().c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char prefix[96] = {};
    wsprintfA(
        prefix,
        "[%02u:%02u:%02u.%03u][tid=%lu] ",
        st.wHour,
        st.wMinute,
        st.wSecond,
        st.wMilliseconds,
        GetCurrentThreadId());

    DWORD written = 0;
    WriteFile(file, prefix, static_cast<DWORD>(lstrlenA(prefix)), &written, nullptr);
    WriteFile(file, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

struct StubPlugin
{
    void** vtable;
};

void __fastcall PluginTerm(StubPlugin*, void*)
{
    Log("plugin vtable +0x08 term");
}

uint32_t __fastcall PluginReady(StubPlugin*)
{
    Log("plugin vtable +0x10 ready");
    return 1;
}

uint32_t __fastcall PluginAttach(StubPlugin*, void*)
{
    Log("plugin vtable +0x18 attach");
    return 1;
}

uint32_t __fastcall PluginInit(StubPlugin*, void*, void*, void*, void*, void*)
{
    Log("plugin vtable +0x30 init");
    return 1;
}

uint32_t __fastcall PluginQuery(StubPlugin*, void*)
{
    Log("plugin vtable +0x60 query");
    return 1;
}

uint32_t __fastcall PluginUnknown(StubPlugin*, uintptr_t, uintptr_t, uintptr_t, uintptr_t)
{
    Log("plugin vtable unknown slot");
    return 1;
}

void* g_pluginVtable[] = {
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginTerm),
    reinterpret_cast<void*>(&PluginReady),
    reinterpret_cast<void*>(&PluginAttach),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginInit),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginUnknown),
    reinterpret_cast<void*>(&PluginQuery),
};

StubPlugin g_plugin = {g_pluginVtable};

void* __fastcall CreatePlugin(void*)
{
    Log("createPlugin");
    return &g_plugin;
}

void* __fastcall CreateParams(void*)
{
    Log("createParams");
    return nullptr;
}

struct PluginListItem
{
    PluginListItem* next;
    uint8_t type;
    uint8_t reserved09[3];
    uint32_t companyId;
    uint32_t pluginId;
    uint32_t reserved14;
    void* createPlugin;
    void* createParams;
    void* reserved28;
    void* reserved30;
    void* registeredCallback;
    void* registeredCallbackUser;
    void* thirdCallback;
    void* codecField;
};

static_assert(offsetof(PluginListItem, type) == 0x08);
static_assert(offsetof(PluginListItem, companyId) == 0x0C);
static_assert(offsetof(PluginListItem, pluginId) == 0x10);
static_assert(offsetof(PluginListItem, createPlugin) == 0x18);
static_assert(offsetof(PluginListItem, createParams) == 0x20);
static_assert(offsetof(PluginListItem, thirdCallback) == 0x48);
static_assert(offsetof(PluginListItem, codecField) == 0x50);

PluginListItem g_pluginEntry = {
    nullptr,
    kPluginType,
    {},
    kCompanyId,
    kPluginId,
    0,
    reinterpret_cast<void*>(&CreatePlugin),
    reinterpret_cast<void*>(&CreateParams),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};
}

extern "C" __declspec(dllexport) PluginListItem* g_pAKPluginList = &g_pluginEntry;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        Log("DLL_PROCESS_ATTACH");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Log("DLL_PROCESS_DETACH");
    }

    return TRUE;
}
