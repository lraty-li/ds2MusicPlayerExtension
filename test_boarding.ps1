# DS2 Boarding Test - SendInput scancode approach
$siClassName = "SI_$([Guid]::NewGuid().ToString('N'))"
$siSource = @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class $siClassName {
    const int SW_RESTORE = 9;
    const uint INPUT_KEYBOARD = 1;
    const uint KEYEVENTF_KEYUP = 0x0002;
    const uint KEYEVENTF_SCANCODE = 0x0008;

    [StructLayout(LayoutKind.Sequential)]
    struct INPUT {
        public uint type;
        public INPUTUNION U;
    }

    [StructLayout(LayoutKind.Explicit)]
    struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public HARDWAREINPUT hi;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public UIntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct KEYBDINPUT {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public UIntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct HARDWAREINPUT {
        public uint uMsg;
        public ushort wParamL;
        public ushort wParamH;
    }

    [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")] static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder t, int max);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr h);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr lp);

    [StructLayout(LayoutKind.Sequential)]
    struct RECT { public int left, top, right, bottom; }

    public static string GetTitle(IntPtr h) {
        var sb = new StringBuilder(256);
        GetWindowText(h, sb, sb.Capacity);
        return sb.ToString();
    }

    public static bool IsWindowHandle(IntPtr h) {
        return IsWindow(h);
    }

    public static void Click(IntPtr hwnd) {
        Focus(hwnd);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }

    public static void ClickRel(IntPtr hwnd, double rx, double ry) {
        Focus(hwnd);
        RECT r; if (!GetWindowRect(hwnd, out r)) return;
        int x = r.left + (int)((r.right - r.left) * rx);
        int y = r.top + (int)((r.bottom - r.top) * ry);
        SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(50);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
    }

    static uint SendScan(ushort scancode, bool keyUp) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].U.ki.wVk = 0;
        inputs[0].U.ki.wScan = scancode;
        inputs[0].U.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        return SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
    }

    public static bool KeyScan(IntPtr hwnd, ushort scancode, int holdMs) {
        bool focused = Focus(hwnd);
        uint down = SendScan(scancode, false);
        System.Threading.Thread.Sleep(holdMs);
        uint up = SendScan(scancode, true);
        System.Threading.Thread.Sleep(80);

        bool ok = focused && down == 1 && up == 1;
        if (!ok) {
            Console.WriteLine("WARN key scan=0x{0:X} focused={1} down={2} up={3}", scancode, focused, down, up);
        }
        return ok;
    }

    public static bool Focus(IntPtr hwnd) {
        IntPtr fg = GetForegroundWindow();
        uint fgTid = GetWindowThreadProcessId(fg, IntPtr.Zero);
        uint myTid = GetCurrentThreadId();
        bool attached = false;
        if (fgTid != 0 && fgTid != myTid) {
            attached = AttachThreadInput(myTid, fgTid, true);
        }

        ShowWindow(hwnd, SW_RESTORE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        System.Threading.Thread.Sleep(200);

        if (attached) {
            AttachThreadInput(myTid, fgTid, false);
        }
        return GetForegroundWindow() == hwnd;
    }
}
"@
Add-Type -TypeDefinition $siSource
$SI = [type]$siClassName

$gameDir = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$logPath = Join-Path $gameDir "log.txt"
Write-Host "=== DS2 Boarding Test (SendInput scancode) ==="
if (Test-Path $logPath) { Clear-Content $logPath -EA SilentlyContinue }

Write-Host "Cleaning stale game processes..."
& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Start-Sleep 2

Write-Host "Launching via Steam..."
Start-Process "steam://rungameid/3280350"
$p = $null
for ($i = 0; $i -lt 60; $i++) {
    $p = Get-Process -Name "DS2" -ErrorAction SilentlyContinue
    if ($p -and $p.MainWindowHandle -ne 0) { break }
    Start-Sleep 1
}
if (!$p -or $p.MainWindowHandle -eq 0) { Write-Host "FAIL"; exit 1 }
Write-Host "DS2 PID: $($p.Id)"
$hwnd = $p.MainWindowHandle
Write-Host "DS2 hwnd: 0x$($hwnd.ToString('X')) title=`"$($SI::GetTitle($hwnd))`""
$script:gamePid = $p.Id
$script:gameHwnd = $hwnd

function Enter-LauncherIfPresent {
    param([System.Diagnostics.Process]$Process)

    $launcherHwnd = $Process.MainWindowHandle
    if ($launcherHwnd -eq [IntPtr]::Zero) { return $Process }

    $title = $SI::GetTitle($launcherHwnd)
    if ($title -notmatch '^DEATH STRANDING 2: ON THE BEACH$') { return $Process }

    Write-Host "Launcher detected, launcher clicks disabled"
    # $SI::ClickRel($launcherHwnd, 0.871, 0.930)
    Start-Sleep -Milliseconds 250
    # $SI::ClickRel($launcherHwnd, 0.742, 0.699)

    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 1
        $next = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } |
            Select-Object -First 1
        if ($next -and $SI::GetTitle($next.MainWindowHandle) -match 'v\d') {
            Write-Host "Game window: 0x$($next.MainWindowHandle.ToString('X')) title=`"$($SI::GetTitle($next.MainWindowHandle))`""
            return $next
        }
    }

    return $Process
}

$p = Enter-LauncherIfPresent $p
$hwnd = $p.MainWindowHandle
$script:gamePid = $p.Id
$script:gameHwnd = $hwnd

function Get-CrashReportProcesses {
    @(Get-Process -Name "crs-handler" -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowTitle -eq "Report Problem" })
}

function Close-CrashReportWindow {
    param([object[]]$Reports = $(Get-CrashReportProcesses))

    foreach ($report in $Reports) {
        Write-Host "Closing crash report window (pid $($report.Id))"
        [void]$report.CloseMainWindow()
        Start-Sleep -Milliseconds 500

        $stillRunning = Get-Process -Id $report.Id -ErrorAction SilentlyContinue
        if ($stillRunning) {
            Stop-Process -Id $report.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

function Stop-IfGameCrashed {
    param([string]$Before)

    $reports = Get-CrashReportProcesses
    if ($reports.Count -gt 0) {
        Close-CrashReportWindow $reports
        Write-Host "FAIL: DS2 crashed before $Before (Report Problem window opened)"
        exit 1
    }

    $current = Get-Process -Id $script:gamePid -ErrorAction SilentlyContinue
    if (!$current) {
        Close-CrashReportWindow
        Write-Host "FAIL: DS2 crashed/exited before $Before (pid $script:gamePid is gone)"
        exit 1
    }

    if ($script:gameHwnd -eq [IntPtr]::Zero -or -not $SI::IsWindowHandle($script:gameHwnd)) {
        Close-CrashReportWindow
        Write-Host "FAIL: DS2 crashed/exited before $Before (window handle is gone)"
        exit 1
    }
}

function Wait-GameSeconds {
    param([string]$Label, [int]$Seconds)
    Write-Host "$Label ($($Seconds)s)"
    for ($i = 0; $i -lt $Seconds; $i++) {
        Stop-IfGameCrashed "waiting for $Label"
        Start-Sleep 1
    }
    Stop-IfGameCrashed "after $Label"
}

function Wait-GameMilliseconds {
    param([string]$Label, [int]$Milliseconds)
    Stop-IfGameCrashed "waiting for $Label"
    Start-Sleep -Milliseconds $Milliseconds
    Stop-IfGameCrashed "after $Label"
}

function Send-GameKey {
    param([UInt16]$Scan, [string]$Name, [int]$HoldMs = 60)
    Stop-IfGameCrashed "sending $Name"
    Write-Host "  Focus + $Name (SendInput scancode 0x$($Scan.ToString('X')))"
    [void]$SI::KeyScan($hwnd, $Scan, $HoldMs)
}

Wait-GameSeconds "Intro" 18
Send-GameKey 0x1C "ENTER (SKIP)"

Wait-GameSeconds "Wait" 4
Send-GameKey 0x1C "ENTER (CONTINUE)"
Wait-GameSeconds "Recover prompt" 1
Send-GameKey 0x1E "A (RECOVER YES)"
Wait-GameSeconds "Recover confirm delay" 1
Send-GameKey 0x1C "ENTER (CONFIRM RECOVER)"

Wait-GameSeconds "Load" 4
Send-GameKey 0x21 "F (BOARD)"

Wait-GameSeconds "Ride" 4
Send-GameKey 0x21 "F (DISMOUNT)"

Wait-GameSeconds "Quit" 4
Send-GameKey 0x01 "ESC"; Wait-GameSeconds "Quit menu" 2
Send-GameKey 0x11 "W"; Wait-GameMilliseconds "Quit select" 200
Send-GameKey 0x1C "ENTER"; Wait-GameSeconds "Quit confirm" 1
Send-GameKey 0x1E "A"; Wait-GameMilliseconds "Quit accept" 200
Send-GameKey 0x1C "ENTER"
Write-Host "=== Done ==="
