# 标签页音频实时 PCM 扩展

这个目录提供浏览器侧捕获组件，不再需要 Node bridge。

```text
Chrome/Edge tabCapture
  -> AudioWorklet
  -> Float32LE stereo chunks
  -> ws://127.0.0.1:47832
  -> ds2_dll_music_resource.dll 内置 WebSocket server
```

## 运行

1. 可以先启动游戏，也可以先点击扩展等待游戏启动。
2. 在 Chrome/Edge 扩展页加载本目录。
3. 打开一个播放音乐的标签页。
4. 点击扩展图标。
   - 游戏端尚未监听时显示 `WAIT`，扩展会自动重试连接。
   - 连接到 `ds2_dll_music_resource.dll` 后显示 `PCM` 并开始推流。
5. 再次点击扩展图标停止捕获。

当前扩展处于受控模式：没有 popup 测试界面，点击扩展图标只负责开始/停止推流；暂停/恢复由游戏状态通过 WebSocket 控制。

## 游戏暂停同步

runtime DLL 会通过同一条 WebSocket 向扩展发送控制消息：

```json
{"type":"control","command":"pause","reason":"auto_block"}
{"type":"control","command":"resume","reason":"auto_block"}
{"type":"control","command":"pause","reason":"manual"}
{"type":"control","command":"resume","reason":"manual"}
```

扩展收到后会先探测当前捕获 tab 的所有 frame，选择最像 active media 的 frame 执行控制，并以游戏内播放器状态为准设置浏览器播放状态。游戏说暂停时只暂停仍在播放的媒体；游戏说恢复时只恢复仍处于暂停状态的媒体，避免把控制做成反复 toggle。

`auto_block` 的自动暂停会延迟 1.5s 执行，用来保留某些场景下的游戏内渐弱效果；若这期间收到对应恢复消息，会取消本次暂停。

控制脚本运行在页面 MAIN world，当前顺序为：

```text
adapters/youtube.js: movie_player.pauseVideo()/playVideo()
adapters/netease.js: 暂停用 audio.pause()；恢复点击网易云播放栏按钮
adapters/media_session_hook.js: 无站点 adapter 时调用网页通过 Media Session 注册的 play/pause handler
```

Media Session hook 只作为 fallback adapter 存在：YouTube、网易云等有站点 adapter 的页面不会走 hook 控制。为降低对网页加载的影响，早期 hook 只注入到 Spotify 域名；其他未知站点只会在控制时动态注入，可能无法捕获已经注册过的 handler。标准 Media Session API 仍用于读取网页提供的 title/artist 元数据。扩展更新或重载后，已打开的 Spotify 页面需要刷新一次，hook 才能捕获网页重新注册的 handler。

调试角标：

```text
PAUS/PLAY : 扩展收到了游戏控制消息并执行了页面控制脚本
NOOP      : 页面脚本执行成功，但没有找到可暂停/恢复的媒体，或当前 adapter 未支持该命令
CTRL      : 页面控制脚本注入失败
NOID      : 控制消息缺少目标 tabId
```

扩展只为 YouTube、网易云和 Spotify 请求站点权限；Spotify 需要早期 hook 来兼容 Media Session 控制。更新扩展后浏览器可能要求重新确认权限。

网易云冷启动播放受 Chrome autoplay policy 约束。如果页面从未由用户交互启动过有声播放，脚本侧恢复仍可能被浏览器拒绝。网易云 adapter 不直接调用 `audio.play()`，而是点击页面播放栏按钮，避免直接 `audio.play()` 曾出现的从头播放问题。

## 音频包

默认发送 v2 Float32LE，避免 WebAudio Float32 被量化到 PCM16 后再还原。
运行时 DLL 仍兼容旧 v1 PCM16 包。

WebSocket binary payload v2：

```text
u32 magic        "DS2A" little-endian
u16 version      2
u16 channels     2
u32 sampleRate   48000
u32 frameCount   当前为 480
u64 sequence
u32 payloadBytes
u16 sampleFormat 2 = Float32LE
u16 headerBytes  32
bytes Float32 interleaved little-endian
```

每包约 10ms：

```text
480 frames * 2 channels * 4 bytes = 3840 bytes
```

旧 v1 PCM16 payload：

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
