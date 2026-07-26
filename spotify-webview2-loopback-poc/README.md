# Spotify Connect WebView2 Helper

同一个程序同时提供可见的独立诊断模式和 ASI 启动的隐藏 helper 模式。
它不加载浏览器扩展，也不把 Spotify PCM 写入磁盘。

当前验证的最终候选链路：

```text
Spotify Web Playback SDK
  → 独立 Win32 WebView2 宿主
  → HTMLMediaElement
  → MediaElementAudioSourceNode
  → AudioWorklet（失败时显式回退 ScriptProcessor）
  → 20 ms 的 48 kHz / 双声道 / PCM16 分块
  → WebView2 SharedBuffer 环形缓冲区
  → 小型 WebMessage 仅通知原生消费槽位
  → 原生 C++ 连续性、吞吐量、RMS、Peak、滚动校验
  → 重分为现有游戏协议的 480 帧 / 10 ms 数据包
  → ws://127.0.0.1:47832
  → 独立 probe 或游戏内 Wwise SourcePlugin 实时拉取
```

Windows Process Loopback 只在可见诊断模式中运行，作为接管前后的对照
观测；隐藏 helper 不启动这条采集路径。

Helper 连接游戏后先注册为 `spotify_connect` 来源，但不会仅因连接成功而
抢占 tabCapture。只有 Spotify SDK 从暂停/非活跃状态进入真实播放时才发送
`source_claim`；被 tabCapture 抢占时，游戏会沿原反向控制通道暂停本实例。
再次通过 Spotify Connect 播放后会重新声明，并重发缓存的曲名与封面。

## 运行模式

- 不带参数启动：显示完整诊断窗口，可配合独立 probe 验证，不启动游戏；
- 带 `--game-helper` 启动：tool window 保持 WebView2 可见，但位于虚拟桌面
  之外且不进入任务栏/Alt-Tab；自动接管播放中的 Spotify 媒体、切到
  silent sink，并连接本机游戏音频协议；
- helper 模式首次缺少 PKCE 授权时才显示窗口，授权完成后自动隐藏。

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
  DS2SpotifyWebView2Helper.exe
  spotify_game_stream_probe.exe
```

普通 SharedBuffer 验证可以执行 `.\start.ps1`。游戏协议替身验证先启动
`spotify_game_stream_probe.exe`，再启动 `DS2SpotifyWebView2Helper.exe`。
probe 只监听本机回环地址，不连接或加载游戏。

隐藏模式可直接执行：

```powershell
.\DS2SpotifyWebView2Helper.exe --game-helper
```

## Spotify Developer App 配置

发行配置不会内置测试 Client ID。Spotify 版用户需要 Premium 账号，并创建
自己的 Spotify Developer App：

1. 登录 https://developer.spotify.com/dashboard 并创建 App。
2. 为应用启用 Web Playback SDK。
3. 在 Redirect URIs 中精确添加：

   ```text
   https://appassets.example/index.html
   ```

4. 保存设置，把公开的 Client ID 写入开发配置：

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

发行包对应的配置路径为：

```text
<GameRoot>\scripts\DS2SpotifyHelper\config.json
```

Spotify Development Mode 对授权用户数量有限制；个人用户通常应使用自己
创建的 App。若 App 还要授权其他 Spotify 账号，需要在 Dashboard 的
Users Management 中加入对应账号。

`proxyServer` 可省略；省略时使用 Windows 系统代理。填写后会通过
WebView2 的 `--proxy-server` 参数显式使用该 HTTP、HTTPS 或 SOCKS5 代理。

`appassets.example` 是 WebView2 映射到本地 `web` 目录的 HTTPS 虚拟主机，
不会向该域名发出网络请求。PKCE token 保存在此 PoC 的专属 WebView2 User
Data Folder：

```text
%LOCALAPPDATA%\DS2SpotifyWebView2LoopbackPoc
```

同一目录中的 `standalone-telemetry.log` 保存可见诊断模式的 SDK 页面事件、
宿主静音状态、每 500 ms 的 Process Loopback 指标，以及约每秒一次的原生
Direct PCM 接收统计。每次启动会清空旧日志，避免不同实验混在一起。原始
PCM 分块不会写入日志或磁盘。隐藏模式写入 `helper-telemetry.log`，且没有
Process Loopback 指标。

probe 在 EXE 工作目录写入 `game-stream-probe.log`，只记录数据包、缓冲深度、
欠载和样本统计，不记录原始 PCM。

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
`AudioContext`。点击“接管媒体并启动 PCM 桥接”会调用
`createMediaElementSource()`，把媒体接到新 `AudioContext`，并在
`AnalyserNode` 前插入 PCM tap。tap 优先运行在 AudioWorklet 音频线程，每
960 帧发送一个 PCM16 双声道分块；如果当前文档策略不允许加载 Worklet
模块，界面会明确显示 `ScriptProcessor fallback`。

原生宿主通过 `CreateSharedBuffer` 建立 64 槽环形缓冲区。每个槽可容纳一个
19,200 字节的 PCM 分块；只有确认 frame 的 origin 精确等于
`https://sdk.scdn.co` 后，才以读写权限把缓冲区投递给该 frame。音频分块直接
写入共享槽位，校验值和提交标记最后写入，再通过小型 WebMessage 通知原生
消费。原生端验证槽位版本、格式、序号、长度、提交标记和 FNV32 校验，随后
统计吞吐量、RMS、Peak 与滚动校验并释放槽位；原始 PCM 不写入磁盘。

旧的 WebMessage + Base64 路径只保留为兼容性诊断回退。正式判定要求界面和
原生日志都显示 `transport=shared-ring`；发生 Base64 回退或 ring drop 会明确
判定失败。

原生端把每个 960 帧分块重分为两个 480 帧数据包，使用运行时 SourcePlugin
已经支持的 PCM16 v1 协议发送到 `ws://127.0.0.1:47832`。独立 probe 使用同一
数据包解析规则，并每 10 ms 消费 480 帧，模拟 Wwise 音频回调。页面中的
“probe 发送暂停/恢复”按钮会先向 probe 发诊断请求，再由 probe 沿正式反向
WebSocket 通道发出 `pause` 或 `resume`，最终调用 Spotify Web Playback SDK。

自动化诊断可使用 `F8` 直接切换本地 WebAudio 音调、`F9` 切换宿主静音、
`F10` 滚动到 PCM 指标、`F11` 滚动到日志。F8 不伪造用户手势，因此也能
验证持久 Autoplay 权限是否允许无点击起声。这些快捷键只作用于这个 PoC
窗口。

### 2. Spotify Connect

1. 确认 Client ID 已从 `config.json` 自动加载，并进行一次 PKCE 授权。
2. 授权回调后，SDK 会在没有 DOM 点击和 `activateElement()` 的情况下自动连接。
3. 在手机或桌面 Spotify 的设备列表选择
   `Death Stranding 2`。
4. 播放一首完整曲目，确认曲目信息更新且 PCM 持续非零。
5. 点击“接管媒体并启动 PCM 桥接”，等待至少三秒。
6. 确认原生共享内存、frame 映射均为就绪，传输显示 `shared-ring`。
7. 确认原生判定为“持续收到完整 PCM”，且缺口、乱序、无效块均为零。
8. 再点击“本实例切到 silent sink”。
9. 确认桌面无声，同时 Direct PCM 与原生累计仍持续增长。

### 3. 游戏音频协议替身

1. 先启动 `spotify_game_stream_probe.exe`，再启动 PoC。
2. 确认“游戏音频协议替身”显示已连接。
3. 播放并接管 Spotify 媒体，等待发送端累计至少 100 个数据包。
4. 确认发送端和 probe 均无丢包、无效包、欠载、裁剪或覆盖。
5. 切到 silent sink，确认 probe PCM 仍持续非零。
6. 点击“probe 发送暂停”，确认 Spotify 客户端显示暂停。
7. 点击“probe 发送恢复”，确认 Spotify 客户端恢复播放。
8. 确认恢复后共享内存与游戏协议数据包序号仍连续。

“手动激活并重连”只用于诊断。如果只有点击它之后才能播放，说明当前无感冷启动
目标尚未通过。“接管媒体并启动 PCM 桥接”不可撤销，重复实验需重启 PoC。

helper 模式不执行第 5、8 步：它在检测到播放中的 Spotify 媒体后自动接管，
并在新建 AudioContext 时自动应用 silent sink。

## Go / No-Go

Go 至少要求：

- EME 显示 `Widevine 可用`；
- SDK 没有 `initialization_error` 或 `autoplay_failed`；
- 冷启动后不点击 WebView 页面，Connect 设备仍能自动就绪并接收转移；
- 真实 Spotify 曲目能被 Process Loopback 捕获为连续非零 PCM；
- 媒体接管后的 Direct PCM 持续非零；
- silent sink 后桌面无声且 Direct PCM 仍非零；
- 原生 C++ 连续收到 PCM16 分块，吞吐量约为 48,000 帧/秒；
- PCM 传输为 `shared-ring`，没有 Base64 回退和 ring drop；
- 分块序号缺口、乱序和无效块均为零；
- 游戏协议稳定输出 100 个 480 帧数据包/秒；
- probe 无序号缺口、无效包、欠载、裁剪或覆盖；
- probe 反向 pause/resume 能改变 Spotify 的真实播放状态；
- 其他程序出声不会进入本 PoC 的指标。

任何一项失败都应停留在 standalone 阶段调查，不需要启动游戏。

## 本轮实测结果

2026-07-26 的独立 PoC 实测已满足上述关键条件：

- WebView2 共享缓冲区创建成功，并只投递给 Spotify SDK frame；
- 有声阶段持续以 48,000 帧/秒、192,000 字节/秒接收双声道 PCM16；
- 切换 silent sink 后，Process Loopback 回到
  `RMS 0.0000146 / Peak 0.0000305` 的静默基线；
- 同期 Direct PCM 仍为非零，例如
  `RMS 0.2600084 / Peak 1.0000000`；
- 累计 1,411 个共享内存分块时，序号缺口、乱序和无效块仍全部为零；
- 全程传输类型仅为 `shared-ring`，未回退 Base64；
- 游戏协议发送端稳定输出 100 包/秒，队列峰值仅 2–3 包；
- probe 累计超过 15,000 包时仍保持序号缺口、无效包、欠载、补静音、
  裁剪和覆盖全部为零；
- silent sink 下 probe PCM 仍持续非零，缓冲保持在实时低延迟范围；
- probe 反向 pause 和 resume 均被 WebView2 宿主接收并由 Spotify SDK
  成功执行，恢复后 PCM 序号继续连续。
- helper 模式的屏幕外 tool window 可在没有 WebView2 页面点击的情况下完成
  Connect 转移；真正隐藏顶层 HWND 会使 SDK 只更新播放状态而不创建媒体
  渲染管线，因此正式模式不会使用 `SW_HIDE`。
- helper 检测到媒体元素后可自动接管并应用 silent sink；实测超过 3,700 个
  游戏协议包时，探针仍为零缺口、零无效包、零欠载、零裁剪和零覆盖。
