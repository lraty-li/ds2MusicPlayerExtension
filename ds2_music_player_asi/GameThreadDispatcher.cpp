#include "pch.h"

#include "GameThreadDispatcher.h"

#include <atomic>
#include <mutex>
#include <sstream>

namespace
{
constexpr UINT kDispatchMessage = WM_APP + 0x534;

std::mutex g_mutex;
HWND g_window = nullptr;
WNDPROC g_originalWndProc = nullptr;
std::atomic<GameThreadDispatcher::Callback> g_callback{nullptr};
bool g_missingWindowLogged = false;

void Log(const Logger& logger, const std::string& text)
{
    logger.Log(text);
}

BOOL CALLBACK PickProcessWindow(HWND hwnd, LPARAM param)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId())
    {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER))
    {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd))
    {
        return TRUE;
    }

    *reinterpret_cast<HWND*>(param) = hwnd;
    return FALSE;
}

HWND FindProcessWindow()
{
    HWND hwnd = nullptr;
    EnumWindows(PickProcessWindow, reinterpret_cast<LPARAM>(&hwnd));
    return hwnd;
}

LRESULT CALLBACK DispatchWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

bool InstallWndProc(HWND hwnd, const Logger& logger)
{
    SetLastError(ERROR_SUCCESS);
    LONG_PTR previous = SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DispatchWndProc));
    if (!previous && GetLastError() != ERROR_SUCCESS)
    {
        Log(logger, "game thread dispatcher skipped: SetWindowLongPtr failed");
        return false;
    }

    g_window = hwnd;
    g_originalWndProc = reinterpret_cast<WNDPROC>(previous);
    return true;
}

LRESULT CALLBACK DispatchWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == kDispatchMessage)
    {
        auto callback = g_callback.exchange(nullptr);
        if (callback)
        {
            callback();
        }
        return 0;
    }
    if (g_originalWndProc)
    {
        return CallWindowProcW(g_originalWndProc, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
} // namespace

namespace GameThreadDispatcher
{
bool EnsureInstalled(const Logger& logger)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_window && IsWindow(g_window))
    {
        const LONG_PTR current = GetWindowLongPtrW(g_window, GWLP_WNDPROC);
        if (current == reinterpret_cast<LONG_PTR>(&DispatchWndProc))
        {
            return true;
        }
        return InstallWndProc(g_window, logger);
    }

    HWND hwnd = FindProcessWindow();
    if (!hwnd)
    {
        if (!g_missingWindowLogged)
        {
            Log(logger, "game thread dispatcher skipped: process window not found");
            g_missingWindowLogged = true;
        }
        return false;
    }

    if (!InstallWndProc(hwnd, logger)) return false;

    DWORD pid = 0;
    const DWORD threadId = GetWindowThreadProcessId(hwnd, &pid);
    std::ostringstream oss;
    oss << "game thread dispatcher installed hwnd=" << hwnd
        << " threadId=" << threadId;
    Log(logger, oss.str());
    return true;
}

bool Post(Callback callback)
{
    HWND hwnd = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_window || !IsWindow(g_window))
        {
            return false;
        }
        hwnd = g_window;
    }

    g_callback.store(callback);
    return PostMessageW(hwnd, kDispatchMessage, 0, 0) != 0;
}
} // namespace GameThreadDispatcher
