$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class WinApi {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    public static void Find(string match) {
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            StringBuilder sb = new StringBuilder(256);
            GetWindowText(hWnd, sb, 256);
            string title = sb.ToString().ToLower();
            if (title.Contains(match.ToLower()) && title.Length > 0) {
                uint pid; GetWindowThreadProcessId(hWnd, out pid);
                try {
                    var p = System.Diagnostics.Process.GetProcessById((int)pid);
                    Console.WriteLine("FOUND: {0} PID={1} HWND=0x{2:X} TITLE=\"{3}\"", p.ProcessName, pid, (long)hWnd, title);
                } catch { }
            }
            return true;
        }, IntPtr.Zero);
    }
}
'@
[WinApi]::Find("report")
[WinApi]::Find("problem")
