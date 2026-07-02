$siClassName = "Cap_$([Guid]::NewGuid().ToString('N'))"
$siSource = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;

public static class $siClassName {
    const int SW_RESTORE = 9;
    const uint INPUT_KEYBOARD = 1;
    const uint KEYEVENTF_KEYUP = 0x0002;
    const uint KEYEVENTF_SCANCODE = 0x0008;

    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public INPUTUNION U; }
    [StructLayout(LayoutKind.Explicit)] struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
        [FieldOffset(0)] public HARDWAREINPUT hi;
    }
    [StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT {
        public int dx; public int dy; public uint mouseData; public uint dwFlags;
        public uint time; public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT {
        public ushort wVk; public ushort wScan; public uint dwFlags;
        public uint time; public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)] struct HARDWAREINPUT {
        public uint uMsg; public ushort wParamL; public ushort wParamH;
    }
    [StructLayout(LayoutKind.Sequential)] struct RECT {
        public int left, top, right, bottom;
    }

    [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")] static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetWindowText(IntPtr h, StringBuilder t, int max);
    [DllImport("user32.dll")] static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr lp);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();

    public static string GetTitle(IntPtr h) {
        var sb = new StringBuilder(256);
        GetWindowText(h, sb, sb.Capacity);
        return sb.ToString();
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
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(80);
        Click(hwnd);
    }

    public static void MouseMove(IntPtr hwnd, int dx, int dy) {
        Focus(hwnd);
        mouse_event(0x0001, dx, dy, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(80);
    }

    static uint SendScan(ushort scancode, bool keyUp) {
        INPUT[] inputs = new INPUT[1];
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].U.ki.wVk = 0;
        inputs[0].U.ki.wScan = scancode;
        inputs[0].U.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
        return SendInput(1, inputs, System.Runtime.InteropServices.Marshal.SizeOf(typeof(INPUT)));
    }

    public static void KeyScan(IntPtr hwnd, ushort scancode, int holdMs) {
        Focus(hwnd);
        SendScan(scancode, false);
        System.Threading.Thread.Sleep(holdMs);
        SendScan(scancode, true);
        System.Threading.Thread.Sleep(80);
    }

    public static bool Focus(IntPtr hwnd) {
        IntPtr fg = GetForegroundWindow();
        uint fgTid = GetWindowThreadProcessId(fg, IntPtr.Zero);
        uint myTid = GetCurrentThreadId();
        bool attached = false;
        if (fgTid != 0 && fgTid != myTid) attached = AttachThreadInput(myTid, fgTid, true);
        ShowWindow(hwnd, SW_RESTORE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        System.Threading.Thread.Sleep(200);
        if (attached) AttachThreadInput(myTid, fgTid, false);
        return GetForegroundWindow() == hwnd;
    }

    public static void CaptureWindow(IntPtr hwnd, string path) {
        Focus(hwnd);
        RECT r; if (!GetWindowRect(hwnd, out r)) return;
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        using (var bmp = new Bitmap(w, h)) {
            using (var g = Graphics.FromImage(bmp)) {
                g.CopyFromScreen(r.left, r.top, 0, 0, new Size(w, h));
            }
            bmp.Save(path, ImageFormat.Png);
        }
    }
}
"@

Add-Type -TypeDefinition $siSource -ReferencedAssemblies System.Drawing
$SI = [type]$siClassName
$gameDir = "F:\SteamLibrary\steamapps\common\DEATH STRANDING 2 - ON THE BEACH"
$outDir = Join-Path $PSScriptRoot "build\boarding_capture\current"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Remove-Item -LiteralPath (Join-Path $outDir "*.png") -Force -ErrorAction SilentlyContinue
if (Test-Path (Join-Path $gameDir "log.txt")) {
    Clear-Content (Join-Path $gameDir "log.txt") -EA SilentlyContinue
}

& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Start-Sleep 2
Start-Process "steam://rungameid/3280350"
$p = $null
for ($i = 0; $i -lt 60; $i++) {
    $p = Get-Process -Name "DS2" -ErrorAction SilentlyContinue
    if ($p -and $p.MainWindowHandle -ne 0) { break }
    Start-Sleep 1
}
if (!$p -or $p.MainWindowHandle -eq 0) { Write-Host "FAIL launch"; exit 1 }
$hwnd = $p.MainWindowHandle
if ($SI::GetTitle($hwnd) -match '^DEATH STRANDING 2: ON THE BEACH$') {
    $SI::ClickRel($hwnd, 0.871, 0.930)
    Start-Sleep -Milliseconds 250
    $SI::ClickRel($hwnd, 0.742, 0.699)
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep 1
        $next = Get-Process -Name "DS2" -ErrorAction SilentlyContinue |
            Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
        if ($next -and $SI::GetTitle($next.MainWindowHandle) -match 'v\d') {
            $p = $next; $hwnd = $p.MainWindowHandle; break
        }
    }
}

function Wait-Safe([int]$seconds) {
    for ($i = 0; $i -lt $seconds; $i++) {
        if (!(Get-Process -Id $p.Id -ErrorAction SilentlyContinue)) { exit 1 }
        Start-Sleep 1
    }
}
function Key([UInt16]$scan, [int]$hold = 60) { $SI::KeyScan($hwnd, $scan, $hold) }
function MouseMove([int]$dx, [int]$dy = 0) { $SI::MouseMove($hwnd, $dx, $dy) }
function Shot([string]$name) { $SI::CaptureWindow($hwnd, (Join-Path $outDir $name)) }

Wait-Safe 15
$SI::Click($hwnd)
Wait-Safe 5
Key 0x1C
Wait-Safe 2
Key 0x1E
Wait-Safe 2
Key 0x1C
Wait-Safe 2

Shot "00_before_board.png"
Key 0x21
Start-Sleep -Milliseconds 150; Shot "01_board_150ms.png"
Start-Sleep -Milliseconds 350; Shot "02_board_500ms.png"
Start-Sleep -Milliseconds 700; Shot "03_board_1200ms.png"
Start-Sleep -Milliseconds 1000; Shot "04_board_2200ms.png"
Start-Sleep -Milliseconds 1500; Shot "05_board_3700ms.png"

Key 0x21
Start-Sleep 12
Shot "06_after_dismount.png"

& (Join-Path $PSScriptRoot "kill_ds2.ps1")
Write-Host "CAPTURE_OK $outDir"
