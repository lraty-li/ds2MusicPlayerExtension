# Wwise SourcePlugin 外部音频流方案

本文记录当前已经验证可工作的 DS2 音乐播放器外部音频流方案。目标是在游戏内音乐播放器中新增一首特殊曲目，点击播放时不播放游戏内 WEM，而是由自定义 Wwise Source Plugin 从浏览器标签页捕获音频流并实时输出给 Wwise。

## 最终结论

可行链路如下：

```text
Chrome/Edge tabCapture
  -> AudioWorklet 输出 PCM16 stereo
  -> WebSocket 发送到 ds2_dll_music_resource.dll 内置本地 server
  -> DLL 内 float 环形缓冲
  -> Wwise SourcePlugin::Execute()
  -> 游戏内特殊曲目播放浏览器音频
```

游戏侧在 `DSMusicPlayerSystemResource` 加载完成后追加一个可见特殊曲目。音频侧加载一个合法的 `Event -> Action -> Sound -> SourcePlugin` SoundBank，由自定义 SourcePlugin 提供实时 PCM。

## 关键版本

DS2 使用的 SoundBank 版本为：

```text
BKHD version = 0x96 = 150
```

因此 SourcePlugin bank 必须由能生成 bank version 150 的 Wwise 版本生成。当前验证成功版本：

```text
Wwise 2023.1.8.8601
```

SDK/Authoring 的本机安装路径由各构建脚本或工程配置维护；文档中的仓库内路径统一使用相对路径。

## 工程位置

ASI 注入工程：

```text
ds2_music_player_asi
```

运行时 SourcePlugin DLL：

```text
ds2_runtime_source_plugin
```

Wwise Authoring 插件：

```text
ds2_dll_music_resource_authoring
```

Wwise 2023 SourcePlugin bank 工程：

```text
ds2_wwise2023_source_bank_project
```

浏览器捕获 MVP：

```text
tab-audio-recorder-mvp
```

当前方案不再需要手动启动 Node bridge，也不要求使用者安装 Node。

2023 生成的合法 bank：

```text
ds2_wwise2023_source_bank_project\GeneratedSoundBanks\Windows\DS2MusicTest.bnk
```

当前目录命名含义：

```text
ds2_music_player_asi
  游戏注入 ASI：注入特殊曲目、注册运行时插件、加载内存 bank。

ds2_runtime_source_plugin
  游戏运行时 DLL：实现 SourcePlugin、接收浏览器 PCM、在 Execute() 输出音频。

ds2_wwise2023_source_bank_project
  Wwise 2023.1.8.8601 工程：生成合法 0x96 SourcePlugin SoundBank 模板。

ds2_dll_music_resource_authoring
  Wwise Authoring 插件工程：让 Wwise Authoring/Bank 生成侧识别自定义 SourcePlugin。

tab-audio-recorder-mvp
  浏览器 tabCapture/AudioWorklet 推流扩展。
```

## 插件 ID

当前插件身份：

```text
PluginName = DS2 DLL Music Resource
CompanyID  = 1703 = 0x6A7
PluginID   = 257  = 0x101
Type       = Source
classId    = 16870002 = 0x01016A72
```

运行时 DLL 和 Authoring 插件 XML 必须保持这些 ID 一致。Bank 中的 SourcePlugin classId 也必须是 `0x01016A72`。

## Bank 模板

合法模板来源是 Wwise 2023.1.8.8601 生成的 `DS2MusicTest.bnk`。

当前 2023 bank 解析结果：

```text
BKHD size = 40
HIRC size = 128
HIRC item count = 4

type 0x11 SourcePlugin
  original object id = 586114608

type 0x02 Sound
  original object id = 255311509
  references SourcePlugin object 586114608

type 0x03 Action
  original object id = 460307619
  references Sound object 255311509
  references Bank id 1261543313

type 0x04 Event
  original object id = 2236792162
  references Action object 460307619
```

嵌入模板文件：

```text
ds2_music_player_asi\GeneratedSourcePluginTemplates.h
ds2_music_player_asi\GeneratedSourcePluginTemplates.cpp
```

Bank 构建代码：

```text
ds2_music_player_asi\SourcePluginBank.cpp
```

运行时替换为固定自定义 ID：

```text
Bank                  0xAD400000
Event                 0xAD100000
Action                0xAD200000
Sound                 0xAD800000
SourcePlugin object   0xAD810000
SourcePlugin classId  0x01016A72
```

## ASI 注入流程

ASI 初始化后执行以下步骤：

1. 安装音乐播放器资源加载监听。
2. 等待 `DSMusicPlayerSystemResource` 加载完成。
3. 向曲目列表追加特殊曲目。
4. 延迟后加载运行时插件 DLL。
5. 调用游戏内 `RegisterPluginDLL` 注册 `ds2_dll_music_resource`。
6. 用 `AK::SoundEngine::LoadBankMemoryCopy` 加载内存中构造的 SourcePlugin bank。

关键日志应为：

```text
RegisterPluginDLL result=1
LoadBankMemoryCopy generated source plugin bank result=1(AK_Success)
source plugin bank loaded
```

特殊曲目当前事件 ID：

```text
0xAD100000
```

这个事件 ID 对应注入到曲目资源中的播放事件。

## 运行时 DLL 注册

运行时 DLL 路径：

```text
<GameRoot>\scripts\ds2_dll_music_resource.dll
```

虽然插件对象和参数对象使用真实 Wwise 2023 SDK 接口：

```cpp
AK::IAkSourcePlugin
AK::IAkPluginParam
```

当前可工作做法是：

```text
导出名仍为 g_pAKPluginList
导出类型对齐 AK::PluginRegistration*
实际指向 DS2 可识别的静态 POD 注册表布局
```

该注册表包含：

```text
type       = AkPluginTypeSource = 2
companyId  = 1703
pluginId   = 257
create     = CreateDS2MusicResource
params     = CreateDS2MusicResourceParams
```

本地 `LoadLibrary` 探针确认导出内容：

```text
type=2
company=1703
plugin=257
create=非空
params=非空
third=0
```

## SourcePlugin 执行

插件入口文件：

```text
ds2_runtime_source_plugin\dllmain.cpp
```

核心实现：

```text
CreateDS2MusicResource()
  -> new Ds2SourcePlugin

Ds2SourcePlugin::GetPluginInfo()
  -> eType = AkPluginTypeSource
  -> uBuildVersion = 517633

Ds2SourcePlugin::Init()
  -> 输出格式设置为 48000 Hz / stereo / float / non-interleaved

Ds2SourcePlugin::Execute()
  -> 从 AudioStreamServer 的 float 环形缓冲读取
  -> 不足部分填 0.0f 静音
  -> eState = AK_DataReady
```

验证日志：

```text
createParams
params Init blockSize=0
createPlugin
plugin GetPluginInfo source build=517633
params Clone
plugin Reset
plugin Init format=48000 stereo float
plugin Execute calls=1 frames=512 channels=2
```

## 浏览器音频管道

浏览器侧位于：

```text
tab-audio-recorder-mvp
```

扩展侧流程：

```text
tabCapture
  -> AudioWorklet
  -> PCM16 stereo chunk
  -> WebSocket ws://127.0.0.1:47832
```

如果游戏端尚未启动或 DLL 尚未监听，扩展点击后显示 `WAIT`，保持捕获会话并每秒重试连接；连接成功后角标切换为 `PCM`。

`ds2_dll_music_resource.dll` 加载后会启动内置 WebSocket server：

```text
127.0.0.1:47832
```

包格式：

```text
u32 magic      0x44533241 ("DS2A")
u16 version    1
u16 channels   2
u32 sampleRate 通常 48000
u32 frameCount 当前通常 480
u64 sequence
u32 pcmBytes
bytes PCM16 interleaved little-endian
```

运行时 DLL 的 `AudioStreamServer` 后台线程接收 WebSocket binary frame，将 PCM16 interleaved 转为 float stereo，并写入低延迟环形缓冲。`Execute()` 在音频线程只做非阻塞读取，不等待 socket。

当前包大小为 480 帧，约 10ms：

```text
480 frames * 2 channels * 2 bytes = 1920 bytes
```

环形缓冲会在积压超过约 100ms 时丢弃旧帧并回落到约 50ms，避免延迟随着推流时间持续堆积。

正常日志示例：

```text
audio websocket listening on 127.0.0.1:47832
audio websocket connected
ws pcm packets=501 frames=240480 drops=0 buffered=...
```

`drops=0` 说明浏览器到 DLL 的序号没有丢包。

## 构建方式

ASI 构建：

```powershell
cd .\ds2_music_player_asi
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

运行时 DLL 构建：

```powershell
cd .\ds2_runtime_source_plugin
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

Authoring 插件构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\ds2_dll_music_resource_authoring\build.ps1
```

运行时 DLL 和 Authoring 插件都应使用 Wwise 2023.1.8.8601 SDK。具体 SDK/Authoring 安装路径以 `ds2_runtime_source_plugin` 工程配置和 `ds2_dll_music_resource_authoring\build.ps1` 为准。

## 已验证成功现象

成功启动并播放特殊曲目时：

```text
RegisterPluginDLL result=1
LoadBankMemoryCopy generated source plugin bank result=1(AK_Success)
source plugin bank loaded
createPlugin
plugin GetPluginInfo source build=517633
plugin Init format=48000 stereo float
plugin Execute calls=...
audio websocket connected
ws pcm packets=... drops=0
```

用户验证结果：

```text
插件能够顺利工作。
```

## 后续可改进点

1. 对浏览器采样率非 48000 的情况做重采样或更明确的拒绝日志。
2. 增加游戏内“下一首/暂停”等操作到浏览器扩展的反向控制消息。
3. 增加淡入/淡出，避免开始播放时缓冲不足导致突兀静音。
4. 将特殊曲目名称固定为清晰可识别字符串。
5. 将 Wwise 2023 bank 解析/模板生成脚本固化，避免手工同步模板。
