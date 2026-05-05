# 标签页音频实时 PCM MVP

这个目录现在验证实时链路：

```text
Chrome/Edge tabCapture
  -> AudioWorklet
  -> PCM16 stereo chunks
  -> ws://127.0.0.1:47832
  -> bridge.js 统计接收
```

## 运行

1. 启动本地 bridge：

```powershell
node E:\dev\code\game\DS2MusicPlayer\RE\tab-audio-recorder-mvp\bridge.js
```

2. 在 Chrome/Edge 扩展页加载本目录。
3. 打开一个播放音乐的标签页。
4. 点击扩展图标，角标显示 `PCM`。
5. bridge 控制台应输出 first packet 和每 5 秒统计。
6. 再次点击扩展图标停止捕获。

## PCM 包

WebSocket binary payload：

```text
u32 magic      "DS2A" little-endian
u16 version    1
u16 channels   2
u32 sampleRate 通常 48000
u32 frameCount 当前为 960
u64 sequence
u32 pcmBytes
bytes PCM16 interleaved little-endian
```

每包约 20ms：

```text
960 frames * 2 channels * 2 bytes = 3840 bytes
```

## 下一步

`bridge.js` 当前只做接收统计。下一步把它改成写共享内存 ring buffer，
再由 `ds2_dll_music_resource.dll` 的后台线程读取。Wwise `Execute()` 只能
非阻塞消费 ring buffer，不直接等待 WebSocket 或浏览器。
