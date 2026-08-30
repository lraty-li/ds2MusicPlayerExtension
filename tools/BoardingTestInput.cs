using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static partial class BoardingTestInput
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
    private struct Rect
    {
        public int left;
        public int top;
        public int right;
        public int bottom;
    }

    private sealed class CapturedFrame : IDisposable
    {
        public int targetMilliseconds;
        public long started;
        public long ended;
        public Bitmap bitmap;

        public void Dispose()
        {
            if (bitmap != null)
                bitmap.Dispose();
        }
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

    [DllImport("kernel32.dll")]
    private static extern ulong GetTickCount64();

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

    public static string KeyScanAndCapture(
        IntPtr window, ushort scanCode, int holdMilliseconds,
        string baseDirectory, string captureName)
    {
        if (!Focus(window))
            return null;

        Rect bounds;
        if (!GetWindowRect(window, out bounds))
            return null;
        int width = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;
        if (width <= 0 || height <= 0)
            return null;

        string directory = Path.Combine(
            baseDirectory, captureName + "_" +
            DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
        Directory.CreateDirectory(directory);
        var frames = new List<CapturedFrame>();
        long origin = 0;
        uint keyUpResult = 0;
        try
        {
            frames.Add(CaptureFrame(bounds, width, height, -1));
            if (SendScanCode(scanCode, false) != 1)
                return null;
            origin = (long)GetTickCount64();
            var keyUpThread = new Thread(() =>
            {
                WaitUntil(origin + holdMilliseconds);
                keyUpResult = SendScanCode(scanCode, true);
            });
            keyUpThread.IsBackground = true;
            keyUpThread.Start();

            int[] targets = { 25, 50, 100, 200, 400 };
            foreach (int target in targets)
            {
                WaitUntil(origin + target);
                frames.Add(CaptureFrame(
                    bounds, width, height, target));
            }
            keyUpThread.Join();
            if (keyUpResult != 1)
                return null;

            var manifest = new List<string>();
            manifest.Add(
                "file,targetMs,captureStartMs,captureMidMs,captureEndMs");
            foreach (CapturedFrame frame in frames)
            {
                long start = frame.started - origin;
                long end = frame.ended - origin;
                long middle = start + (end - start) / 2;
                string actual = middle < 0 ?
                    "m" + (-middle).ToString("D5") :
                    middle.ToString("D5");
                string target = frame.targetMilliseconds < 0 ?
                    "pre" : frame.targetMilliseconds.ToString("D5");
                string fileName = captureName + "_actual_" + actual +
                    "ms_target_" + target + "ms.png";
                frame.bitmap.Save(
                    Path.Combine(directory, fileName), ImageFormat.Png);
                manifest.Add(String.Join(",", new[]
                {
                    fileName,
                    frame.targetMilliseconds.ToString(),
                    start.ToString(),
                    middle.ToString(),
                    end.ToString()
                }));
            }
            File.WriteAllLines(
                Path.Combine(directory, "capture_manifest.csv"), manifest);
            return directory;
        }
        finally
        {
            foreach (CapturedFrame frame in frames)
                frame.Dispose();
        }
    }

    public static int[] GetWindowBounds(IntPtr window)
    {
        Rect rect;
        if (!IsWindow(window) || !GetWindowRect(window, out rect))
            return null;
        return new[] { rect.left, rect.top, rect.right, rect.bottom };
    }

    private static CapturedFrame CaptureFrame(
        Rect bounds, int width, int height, int targetMilliseconds)
    {
        var frame = new CapturedFrame();
        frame.targetMilliseconds = targetMilliseconds;
        frame.started = (long)GetTickCount64();
        using (var source = new Bitmap(
            width, height, PixelFormat.Format24bppRgb))
        {
            using (Graphics graphics = Graphics.FromImage(source))
            {
                graphics.CopyFromScreen(
                    bounds.left, bounds.top, 0, 0,
                    new Size(width, height),
                    CopyPixelOperation.SourceCopy);
            }
            frame.ended = (long)GetTickCount64();
            frame.bitmap = new Bitmap(
                Math.Max(1, width / 2), Math.Max(1, height / 2),
                PixelFormat.Format24bppRgb);
            using (Graphics graphics = Graphics.FromImage(frame.bitmap))
                graphics.DrawImage(source, 0, 0,
                    frame.bitmap.Width, frame.bitmap.Height);
        }
        return frame;
    }

    private static void WaitUntil(long deadline)
    {
        while (true)
        {
            long remaining = deadline - (long)GetTickCount64();
            if (remaining <= 0)
                return;
            if (remaining > 2)
                Thread.Sleep((int)remaining - 1);
            else
                Thread.SpinWait(64);
        }
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
