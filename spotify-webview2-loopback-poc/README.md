# Spotify Connect WebView2 + Process Loopback PoC

这是完全脱离游戏的最小验证程序。它不加载浏览器扩展、不连接游戏、不写入
Spotify PCM，也不实现二维码中继。

它只验证这条链路：

```text
Spotify Web Playback SDK
  → 独立 Win32 WebView2 宿主
  → Windows Process Loopback（WebView2 Browser PID 进程树）
  → RMS / Peak / 非零比例
  → 丢弃 PCM
```

## 当前明确不做

- 不启动或修改游戏；
- 不把 PCM 送进 Wwise；
- 不在游戏专辑图位置显示二维码；
- 不部署公网 OAuth rendezvous；
- 不隐藏 WebView2 窗口。

这些内容都要等本 PoC 的 EME、自动播放、受保护音频捕获和静音矩阵通过之后再做。

## 环境

- Windows 11 x64。Process Loopback API 要求系统 build 20348 或更高，普通
  Windows 10 22H2 不满足；
- Microsoft Edge WebView2 Evergreen Runtime；
- Visual Studio 2022 C++ Desktop 工具链；
- Spotify Premium 测试账号；
- 自己的 Spotify Developer App。

`build.ps1` 固定使用 Microsoft.Web.WebView2 `1.0.4078.44`，首次构建会从
NuGet 官方源下载 SDK 到仓库根目录下已忽略的 `build/deps`。

## 构建

```powershell
cd spotify-webview2-loopback-poc
.\build.ps1
```

输出：

```text
build/spotify-webview2-loopback-poc/Release/
```

也可以执行 `.\start.ps1`；缺少 EXE 时它会先构建。

## Spotify Developer App 配置

1. 创建或打开自己的 Spotify Developer App。
2. 为应用启用 Web Playback SDK。
3. 在 Redirect URIs 中精确添加：

   ```text
   https://appassets.example/index.html
   ```

4. 保存设置，把 Client ID 写入：

   ```text
   spotify-webview2-loopback-poc/config.json
   ```

   ```json
   {
     "spotifyClientId": "你的 Client ID",
     "proxyServer": "http://127.0.0.1:7890"
   }
   ```

构建时该文件会复制到 EXE 旁边；`start.ps1` 每次启动也会同步它。PoC
启动后自动加载，不需要每次粘贴。Client ID 是公开标识；不要把 Client
Secret 写进配置或分发出去。

`proxyServer` 可省略；省略时使用 Windows 系统代理。填写后会通过
WebView2 的 `--proxy-server` 参数显式使用该 HTTP、HTTPS 或 SOCKS5 代理。

`appassets.example` 是 WebView2 映射到本地 `web` 目录的 HTTPS 虚拟主机，
不会向该域名发出网络请求。PKCE token 保存在此 PoC 的专属 WebView2 User
Data Folder：

```text
%LOCALAPPDATA%\DS2SpotifyWebView2LoopbackPoc
```

同一目录中的 `standalone-telemetry.log` 保存当前一次运行的 SDK 页面事件、
宿主静音状态以及每 500 ms 的原始捕获指标。每次启动会清空旧日志，避免不同
实验混在一起。

## 验证顺序

### 1. 本地音调与基础捕获

1. 点击“播放本地 440 Hz 音调”。
2. 确认桌面能听见声音，RMS、Peak 和非零比例持续非零。

“WebView2 内部静音（对照）”和 Windows 会话静音都已证实会在 Process
Loopback 捕获点之前切断 PCM。原生宿主通过
`AddScriptToExecuteOnDocumentCreated`，在顶层页面和每个 Spotify 子 frame
解析前包装 `AudioContext`。各 frame 经跨域消息独立回报对象与 sink 状态；
只有 frame 覆盖完整时才允许判定通过。

Spotify SDK 当前实测使用子 frame 内的 `HTMLMediaElement`，而不是直接创建
`AudioContext`。点击“接管媒体到 Web Audio”会调用
`createMediaElementSource()`，把媒体接到 `AnalyserNode` 和新
`AudioContext`。界面同时显示 Direct RMS/Peak 与原生 Process Loopback，
用于判断受保护媒体是否允许通过 Web Audio 读取。

自动化诊断可使用 `F8` 直接切换本地 WebAudio 音调、`F9` 切换宿主静音、
`F10` 滚动到 PCM 指标、`F11` 滚动到日志。F8 不伪造用户手势，因此也能
验证持久 Autoplay 权限是否允许无点击起声。这些快捷键只作用于这个 PoC
窗口。

### 2. Spotify Connect

1. 确认 Client ID 已从 `config.json` 自动加载，并进行一次 PKCE 授权。
2. 授权回调后，SDK 会在没有 DOM 点击和 `activateElement()` 的情况下自动连接。
3. 在手机或桌面 Spotify 的设备列表选择
   `Death Stranding 2 Helper PoC`。
4. 播放一首完整曲目，确认曲目信息更新且 PCM 持续非零。
5. 点击“接管媒体到 Web Audio”，等待至少三秒。
6. 若 Direct RMS、Peak 持续非零，再点击“本实例切到 silent sink”。
7. 确认桌面无声，同时 Direct PCM 仍持续非零。

“手动激活并重连”只用于诊断。如果只有点击它之后才能播放，说明当前无感冷启动
目标尚未通过。“接管媒体到 Web Audio”不可撤销，重复实验需重启 PoC。

## Go / No-Go

Go 至少要求：

- EME 显示 `Widevine 可用`；
- SDK 没有 `initialization_error` 或 `autoplay_failed`；
- 冷启动后不点击 WebView 页面，Connect 设备仍能自动就绪并接收转移；
- 真实 Spotify 曲目能被 Process Loopback 捕获为连续非零 PCM；
- 媒体接管后的 Direct PCM 持续非零；
- silent sink 后桌面无声且 Direct PCM 仍非零；
- 其他程序出声不会进入本 PoC 的指标。

任何一项失败都应停留在 standalone 阶段调查，不需要启动游戏。
