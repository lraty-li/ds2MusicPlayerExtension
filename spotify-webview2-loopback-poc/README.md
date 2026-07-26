# Spotify Connect WebView2 + Process Loopback PoC

这是完全脱离游戏的最小验证程序。它不加载浏览器扩展、不连接游戏、不写入
Spotify PCM，也不实现二维码中继。

它只验证这条链路：

```text
Spotify Web Playback SDK
  → 独立 Win32 WebView2 宿主
  → Windows Process Loopback（helper 及其 WebView2 子进程）
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
     "spotifyClientId": "你的 Client ID"
   }
   ```

构建时该文件会复制到 EXE 旁边；`start.ps1` 每次启动也会同步它。PoC
启动后自动加载，不需要每次粘贴。Client ID 是公开标识；不要把 Client
Secret 写进配置或分发出去。

`appassets.example` 是 WebView2 映射到本地 `web` 目录的 HTTPS 虚拟主机，
不会向该域名发出网络请求。PKCE token 保存在此 PoC 的专属 WebView2 User
Data Folder：

```text
%LOCALAPPDATA%\DS2SpotifyWebView2LoopbackPoc
```

## 验证顺序

### 1. 本地音调

1. 点击“播放本地 440 Hz 音调”。
2. 确认桌面能听见声音，RMS、Peak 和非零比例持续非零。
3. 点击“静音宿主”。
4. 确认桌面无声，并观察 PCM 是否仍持续非零。

若静音后 PCM 同时归零，先记录为静音路线 No-Go；这不需要启动游戏确认。

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
5. 在宿主静音状态重复播放、暂停、恢复和切歌。

“手动激活并重连”只用于诊断。如果只有点击它之后才能播放，说明当前无感冷启动
目标尚未通过。

## Go / No-Go

Go 至少要求：

- EME 显示 `Widevine 可用`；
- SDK 没有 `initialization_error` 或 `autoplay_failed`；
- 冷启动后不点击 WebView 页面，Connect 设备仍能自动就绪并接收转移；
- 真实 Spotify 曲目能被 Process Loopback 捕获为连续非零 PCM；
- 宿主静音后桌面无声，但捕获 PCM 仍非零；
- 其他程序出声不会进入本 PoC 的指标。

任何一项失败都应停留在 standalone 阶段调查，不需要启动游戏。
