#include "pch.h"

#include "Hooks.h"

#include "FailFast.h"
#include "HookUtils.h"
#include "CustomJacketImageTransfer.h"
#include "Logger.h"
#include "MusicPlayerInjection.h"
#include "PlayStateMonitor.h"
#include "RuntimeEntryTitleRefresh.h"
#include "SpotifyConnectBootstrap.h"
#include "SourceAudioBootstrap.h"

#if defined(DS2_DIAGNOSTIC)
#include <dbghelp.h>
#endif

#include <exception>
#include <sstream>
#include <string>

namespace
{
constexpr wchar_t kExpectedModuleName[] = L"DS2.exe";
Logger g_dllLogger("DllMain");
Logger g_initLogger("Init");

#if defined(DS2_DIAGNOSTIC)
volatile LONG g_writingCrashDump = 0;

std::wstring MakeDiagnosticPath(const wchar_t* extension)
{
    const std::wstring logPath = Logger::GetLogPath();
    const size_t slash = logPath.find_last_of(L"\\/");
    const std::wstring directory = slash == std::wstring::npos ? L"" :
        logPath.substr(0, slash + 1);
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t name[96] = {};
    swprintf_s(name, L"DS2MusicPlayer-diag-%04u%02u%02u-%02u%02u%02u.%03u%s",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds, extension);
    return directory + name;
}

void WriteCrashSummary(EXCEPTION_POINTERS* exceptionInfo, const std::wstring& path,
    const std::wstring& dumpPath)
{
    const DWORD code = exceptionInfo && exceptionInfo->ExceptionRecord ?
        exceptionInfo->ExceptionRecord->ExceptionCode : 0;
    const uintptr_t address = exceptionInfo && exceptionInfo->ExceptionRecord ?
        reinterpret_cast<uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress) : 0;
    const uintptr_t gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const uintptr_t rva = address >= gameBase ? address - gameBase : 0;
    char text[512] = {};
    const int textBytes = sprintf_s(text,
        "DS2MusicPlayer diagnostic crash\r\nexception=0x%08lX\r\naddress=0x%p\r\ngameRva=0x%llX\r\ndump=%ls\r\n",
        code, reinterpret_cast<void*>(address), static_cast<unsigned long long>(rva),
        dumpPath.c_str());
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        if (textBytes > 0) WriteFile(file, text, static_cast<DWORD>(textBytes), &written, nullptr);
        CloseHandle(file);
    }
}

LONG WINAPI DiagnosticUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    if (InterlockedExchange(&g_writingCrashDump, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

    const std::wstring textPath = MakeDiagnosticPath(L".txt");
    const std::wstring dumpPath = MakeDiagnosticPath(L".dmp");
    WriteCrashSummary(exceptionInfo, textPath, dumpPath);

    HMODULE dbgHelp = LoadLibraryW(L"DbgHelp.dll");
    if (dbgHelp)
    {
        using WriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
            const MINIDUMP_EXCEPTION_INFORMATION*, const MINIDUMP_USER_STREAM_INFORMATION*,
            const MINIDUMP_CALLBACK_INFORMATION*);
        const auto writeDump = reinterpret_cast<WriteDumpFn>(
            GetProcAddress(dbgHelp, "MiniDumpWriteDump"));
        HANDLE dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (writeDump && dump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION info = {};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = exceptionInfo;
            info.ClientPointers = FALSE;
            writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                MiniDumpNormal, &info, nullptr, nullptr);
        }
        if (dump != INVALID_HANDLE_VALUE) CloseHandle(dump);
        FreeLibrary(dbgHelp);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void InstallDiagnosticCrashHandler()
{
    SetUnhandledExceptionFilter(DiagnosticUnhandledExceptionFilter);
    g_initLogger.Log("diagnostic build enabled; unhandled-exception dump handler installed");
}
#endif

bool IsCurrentProcessDs2()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0)
    {
        return false;
    }

    std::wstring fullPath = path;
    const size_t pos = fullPath.find_last_of(L"\\/");
    const std::wstring fileName = pos == std::wstring::npos ?
        fullPath :
        fullPath.substr(pos + 1);
    return _wcsicmp(fileName.c_str(), kExpectedModuleName) == 0;
}

void LogModuleInfo(HMODULE selfModule, HMODULE gameModule)
{
    std::ostringstream oss;
    oss << "gameHandle=" << gameModule
        << " selfHandle=" << selfModule
        << " gamePath=" << HookUtils::NarrowUtf8(HookUtils::GetModulePath(gameModule))
        << " selfPath=" << HookUtils::NarrowUtf8(HookUtils::GetModulePath(selfModule));
    g_initLogger.Log(oss.str());
}
}

DWORD WINAPI Hooks::InitThread(LPVOID moduleParam)
{
    if (!IsCurrentProcessDs2())
    {
        return 0;
    }

    DWORD resetError = ERROR_SUCCESS;
    if (!Logger::ResetLogFile(resetError))
    {
        g_initLogger.Log("ResetLogFile failed err=" + std::to_string(resetError));
    }

    g_dllLogger.Log("DLL_PROCESS_ATTACH");
    g_initLogger.Log("begin stream source plugin registration");
#if defined(DS2_DIAGNOSTIC)
    InstallDiagnosticCrashHandler();
#endif

    try
    {
        HMODULE selfModule = reinterpret_cast<HMODULE>(moduleParam);
        HMODULE gameModule = GetModuleHandleW(nullptr);
        SourceAudioBootstrap::Configure(gameModule, selfModule);
        LogModuleInfo(selfModule, gameModule);
        RuntimeEntryTitleRefresh::Configure(gameModule);

        if (!gameModule)
        {
            g_initLogger.Log("GetModuleHandleW(\"DS2.exe\") failed");
            FailFast::Now(g_initLogger, "missing DS2.exe module");
        }

        DWORD gameImageSize = 0;
        if (!HookUtils::TryGetModuleSize(gameModule, gameImageSize))
        {
            g_initLogger.Log("failed to read DS2.exe SizeOfImage");
            FailFast::Now(g_initLogger, "failed to read DS2.exe SizeOfImage");
        }

        std::ostringstream sizeLog;
        sizeLog << "DS2.exe SizeOfImage=" << gameImageSize;
        g_initLogger.Log(sizeLog.str());

        CustomJacketImageTransfer::Start(g_initLogger);

        const bool listenerInstalled =
            MusicPlayerInjection::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(listenerInstalled ? "music player listener installed" :
                                             "music player listener install failed");
        if (!listenerInstalled)
        {
            FailFast::Now(g_initLogger, "music player listener install failed");
        }

        const bool playStateMonitorInstalled =
            PlayStateMonitor::TryInstall(gameModule, g_initLogger);
        g_initLogger.Log(playStateMonitorInstalled ? "play state monitor installed" :
                                                     "play state monitor install failed");
        if (!playStateMonitorInstalled)
        {
            FailFast::Now(g_initLogger, "play state monitor install failed");
        }

        g_initLogger.Log("source audio registration deferred until music resource load");
        SpotifyConnectBootstrap::Start(selfModule, g_initLogger);
    }
    catch (const std::exception& ex)
    {
        g_initLogger.Log(std::string("exception: ") + ex.what());
        FailFast::Now(g_initLogger, "init thread std::exception");
    }
    catch (...)
    {
        g_initLogger.Log("exception: unknown");
        FailFast::Now(g_initLogger, "init thread unknown exception");
    }

    g_initLogger.Log("done");
    return 0;
}

void Hooks::Shutdown(bool processTerminating)
{
    if (!IsCurrentProcessDs2())
    {
        return;
    }

    g_dllLogger.Log("DLL_PROCESS_DETACH");
    if (!processTerminating)
    {
        SpotifyConnectBootstrap::Stop();
    }
}
