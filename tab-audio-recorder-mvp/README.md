# 标签页音频实时 PCM 扩展

这个目录提供浏览器侧捕获组件，不再需要 Node bridge。

```text
Chrome/Edge tabCapture
  -> AudioWorklet
  -> PCM16 stereo chunks
  -> ws://127.0.0.1:47832
  -> ds2_dll_music_resource.dll 内置 WebSocket server
```

## 运行

1. 启动游戏并确保 `ds2_dll_music_resource.dll` 已加载。
2. 在 Chrome/Edge 扩展页加载本目录。
3. 打开一个播放音乐的标签页。
4. 点击扩展图标，角标显示 `PCM` 后开始推流。
5. 再次点击扩展图标停止捕获。

## PCM 包

WebSocket binary payload：

```text
u32 magic      "DS2A" little-endian
u16 version    1
u16 channels   2
u32 sampleRate 48000
u32 frameCount 当前为 480
u64 sequence
u32 pcmBytes
bytes PCM16 interleaved little-endian
```

每包约 10ms：

```text
480 frames * 2 channels * 2 bytes = 1920 bytes
```
