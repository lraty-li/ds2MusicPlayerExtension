using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class BoardingTestInput
{
    private const int SwRestore = 9;
    private const uint InputKeyboard = 1;
    private const uint KeyEventKeyUp = 0x0002;
    private const uint KeyEventScanCode = 0x0008;

    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint type;
        public InputUnion value;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public MouseInput mouse;
        [FieldOffset(0)] public KeyboardInput keyboard;
        [FieldOffset(0)] public HardwareInput hardware;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint flags;
        public uint time;
        public UIntPtr extraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KeyboardInput
    {
        public ushort virtualKey;
        public ushort scanCode;
        public uint flags;
        public uint time;
        public UIntPtr extraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct HardwareInput
    {
        public uint message;
        public ushort lowParam;
        public ushort highParam;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, Input[] inputs, int size);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    private static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern int GetWindowText(IntPtr window, StringBuilder text, int max);

    [DllImport("user32.dll")]
    private static extern bool AttachThreadInput(uint source, uint target, bool attach);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, IntPtr processId);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr window);

    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr window, out Rect rect);

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();

    public static string GetTitle(IntPtr window)
    {
        var text = new StringBuilder(256);
        GetWindowText(window, text, text.Capacity);
        return text.ToString();
    }

    public static bool IsWindowHandle(IntPtr window)
    {
        return IsWindow(window);
    }

    public static bool KeyScan(IntPtr window, ushort scanCode, int holdMilliseconds)
    {
        if (!Focus(window))
            return false;

        uint down = SendScanCode(scanCode, false);
        Thread.Sleep(holdMilliseconds);
        uint up = SendScanCode(scanCode, true);
        Thread.Sleep(80);
        return down == 1 && up == 1;
    }

    public static int[] GetWindowBounds(IntPtr window)
    {
        Rect rect;
        if (!IsWindow(window) || !GetWindowRect(window, out rect))
            return null;
        return new[] { rect.left, rect.top, rect.right, rect.bottom };
    }

    private static uint SendScanCode(ushort scanCode, bool keyUp)
    {
        var inputs = new Input[1];
        inputs[0].type = InputKeyboard;
        inputs[0].value.keyboard.scanCode = scanCode;
        inputs[0].value.keyboard.flags = KeyEventScanCode | (keyUp ? KeyEventKeyUp : 0);
        return SendInput(1, inputs, Marshal.SizeOf(typeof(Input)));
    }

    private static bool Focus(IntPtr window)
    {
        IntPtr foreground = GetForegroundWindow();
        uint foregroundThread = GetWindowThreadProcessId(foreground, IntPtr.Zero);
        uint currentThread = GetCurrentThreadId();
        bool attached = foregroundThread != 0 && foregroundThread != currentThread &&
            AttachThreadInput(currentThread, foregroundThread, true);

        ShowWindow(window, SwRestore);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        Thread.Sleep(200);

        if (attached)
            AttachThreadInput(currentThread, foregroundThread, false);
        return GetForegroundWindow() == window;
    }
}
