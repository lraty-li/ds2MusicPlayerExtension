#include "pch.h"

#include "SpotifyConnectBootstrap.h"

#include "Logger.h"

#include <string>
#include <vector>

namespace
{
constexpr wchar_t kHelperRelativePath[] =
    L"DS2SpotifyHelper\\DS2SpotifyWebView2Helper.exe";

HANDLE g_helperJob = nullptr;
HANDLE g_helperProcess = nullptr;

std::wstring GetSiblingPath(HMODULE module, const wchar_t* name)
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
    {
        return {};
    }

    std::wstring result(path, length);
    const size_t separator = result.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return {};
    }
    result.resize(separator + 1);
    result += name;
    return result;
}

std::wstring ParentFolder(const std::wstring& path)
{
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos
        ? std::wstring()
        : path.substr(0, separator);
}

void CloseHandles()
{
    if (g_helperProcess)
    {
        CloseHandle(g_helperProcess);
        g_helperProcess = nullptr;
    }
    if (g_helperJob)
    {
        CloseHandle(g_helperJob);
        g_helperJob = nullptr;
    }
}

bool CreateKillOnCloseJob(HANDLE& job, DWORD& error)
{
    job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
    {
        error = GetLastError();
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
            job,
            JobObjectExtendedLimitInformation,
            &limits,
            sizeof(limits)))
    {
        error = GetLastError();
        CloseHandle(job);
        job = nullptr;
        return false;
    }
    return true;
}
}

namespace SpotifyConnectBootstrap
{
void Start(HMODULE selfModule, const Logger& logger)
{
    if (g_helperProcess &&
        WaitForSingleObject(g_helperProcess, 0) == WAIT_TIMEOUT)
    {
        return;
    }
    CloseHandles();

    const std::wstring helperPath =
        GetSiblingPath(selfModule, kHelperRelativePath);
    const DWORD attributes = helperPath.empty()
        ? INVALID_FILE_ATTRIBUTES
        : GetFileAttributesW(helperPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        logger.Log("spotify WebView2 helper is missing");
        return;
    }

    DWORD jobError = ERROR_SUCCESS;
    HANDLE job = nullptr;
    if (!CreateKillOnCloseJob(job, jobError))
    {
        logger.Log(
            "spotify helper job creation failed err=" +
            std::to_string(jobError));
        return;
    }

    std::wstring commandLine =
        L"\"" + helperPath + L"\" --game-helper";
    std::vector<wchar_t> mutableCommand(
        commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingFolder = ParentFolder(helperPath);
    if (!CreateProcessW(
            helperPath.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED,
            nullptr,
            workingFolder.c_str(),
            &startup,
            &process))
    {
        const DWORD error = GetLastError();
        CloseHandle(job);
        logger.Log(
            "spotify WebView2 helper launch failed err=" +
            std::to_string(error));
        return;
    }

    const bool assigned =
        AssignProcessToJobObject(job, process.hProcess) != FALSE;
    DWORD lifecycleError = assigned ? ERROR_SUCCESS : GetLastError();
    const bool resumed =
        assigned && ResumeThread(process.hThread) != static_cast<DWORD>(-1);
    if (assigned && !resumed)
    {
        lifecycleError = GetLastError();
    }
    if (!resumed)
    {
        TerminateProcess(process.hProcess, 0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        logger.Log(
            "spotify helper lifecycle setup failed err=" +
            std::to_string(lifecycleError));
        return;
    }

    CloseHandle(process.hThread);
    g_helperJob = job;
    g_helperProcess = process.hProcess;
    logger.Log(
        "spotify WebView2 helper started pid=" +
        std::to_string(process.dwProcessId));
}

void Stop()
{
    if (g_helperJob)
    {
        TerminateJobObject(g_helperJob, 0);
    }
    CloseHandles();
}
}
