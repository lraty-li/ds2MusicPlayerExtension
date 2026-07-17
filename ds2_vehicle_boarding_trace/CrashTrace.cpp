#include "pch.h"
#include "CrashTrace.h"

#include <cstdint>

namespace CrashTrace {
namespace {

uintptr_t g_gameBase = 0;
uintptr_t g_gameEnd = 0;
uintptr_t g_pluginBase = 0;
uintptr_t g_pluginEnd = 0;
volatile LONG g_exceptionCount = 0;

uint32_t ModuleImageSize(HMODULE module)
{
    if (!module)
        return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        reinterpret_cast<uintptr_t>(module) + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->OptionalHeader.SizeOfImage : 0;
}

bool IsFatalCode(DWORD code)
{
    return code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == STATUS_STACK_BUFFER_OVERRUN;
}

void AppendAddress(char* buffer, int& used, const char* label, uintptr_t address)
{
    if (address >= g_gameBase && address < g_gameEnd) {
        used += wsprintfA(buffer + used, " %s=game+0x%I64X",
            label, static_cast<unsigned long long>(address - g_gameBase));
    } else if (address >= g_pluginBase && address < g_pluginEnd) {
        used += wsprintfA(buffer + used, " %s=plugin+0x%I64X",
            label, static_cast<unsigned long long>(address - g_pluginBase));
    } else {
        used += wsprintfA(buffer + used, " %s=0x%I64X",
            label, static_cast<unsigned long long>(address));
    }
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* info)
{
    if (!info || !info->ExceptionRecord ||
        !IsFatalCode(info->ExceptionRecord->ExceptionCode)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedIncrement(&g_exceptionCount) > 4)
        return EXCEPTION_CONTINUE_SEARCH;

    char buffer[1536] = {};
    int used = wsprintfA(buffer, "\r\n[CrashTrace] code=0x%08X",
        info->ExceptionRecord->ExceptionCode);
    AppendAddress(buffer, used, "fault",
        reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress));

    void* frames[24] = {};
    const USHORT count = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < count && used < 1400; ++i) {
        char label[16] = {};
        wsprintfA(label, "stack%u", i);
        AppendAddress(buffer, used, label, reinterpret_cast<uintptr_t>(frames[i]));
    }
    buffer[used++] = '\r';
    buffer[used++] = '\n';

    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash)
        wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"log.txt");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, buffer, static_cast<DWORD>(used), &written, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

bool Install(HMODULE gameModule, uint32_t gameImageSize, HMODULE pluginModule)
{
    g_gameBase = reinterpret_cast<uintptr_t>(gameModule);
    g_gameEnd = g_gameBase + gameImageSize;
    g_pluginBase = reinterpret_cast<uintptr_t>(pluginModule);
    g_pluginEnd = g_pluginBase + ModuleImageSize(pluginModule);
    return AddVectoredExceptionHandler(1, &Handler) != nullptr;
}

} // namespace CrashTrace
