using System;
using System.Collections.Generic;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public static partial class BoardingTestInput
{
    private const uint InputMouse = 0;
    private const uint MouseEventMove = 0x0001;

    public static string KeyScanMouseAndCapture(
        IntPtr window, ushort scanCode, int holdMilliseconds,
        string baseDirectory, string captureName,
        int mouseStepX, int mouseStepY,
        int mouseIntervalMilliseconds, int mouseDurationMilliseconds)
    {
        if (mouseIntervalMilliseconds <= 0 ||
            mouseDurationMilliseconds < 0 ||
            (mouseStepX == 0 && mouseStepY == 0) ||
            !Focus(window))
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
        int mouseFailure = 0;
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

            var mouseThread = new Thread(() =>
            {
                for (int elapsed = 0; elapsed <= mouseDurationMilliseconds;
                    elapsed += mouseIntervalMilliseconds)
                {
                    WaitUntil(origin + elapsed);
                    if (SendRelativeMouse(mouseStepX, mouseStepY) != 1)
                        Interlocked.Exchange(ref mouseFailure, 1);
                }
            });
            mouseThread.IsBackground = true;
            mouseThread.Start();

            int[] targets =
            {
                25, 50, 100, 200, 300, 400, 550, 700, 900, 1200
            };
            foreach (int target in targets)
            {
                WaitUntil(origin + target);
                frames.Add(CaptureFrame(bounds, width, height, target));
            }
            keyUpThread.Join();
            mouseThread.Join();
            if (keyUpResult != 1 || mouseFailure != 0)
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
            File.WriteAllLines(
                Path.Combine(directory, "input_manifest.txt"), new[]
                {
                    "mouseStep=" + mouseStepX + "," + mouseStepY,
                    "mouseIntervalMs=" + mouseIntervalMilliseconds,
                    "mouseDurationMs=" + mouseDurationMilliseconds
                });
            return directory;
        }
        finally
        {
            foreach (CapturedFrame frame in frames)
                frame.Dispose();
        }
    }

    private static uint SendRelativeMouse(int dx, int dy)
    {
        var inputs = new Input[1];
        inputs[0].type = InputMouse;
        inputs[0].value.mouse.dx = dx;
        inputs[0].value.mouse.dy = dy;
        inputs[0].value.mouse.flags = MouseEventMove;
        return SendInput(1, inputs, Marshal.SizeOf(typeof(Input)));
    }
}
