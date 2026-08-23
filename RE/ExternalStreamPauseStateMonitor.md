# 外部音频暂停/恢复状态监视

## 目标

确认游戏内音乐播放器的暂停/恢复语义，并把当前活跃外部音源的暂停状态安全地同步到游戏。

## 方法

1. 查阅 archive 中旧的播放/暂停探索记录。
2. 用 IDA 当前数据库重新确认旧地址对应关系，不直接沿用旧地址。
3. 在 ASI 中用运行时模式扫描定位语义入口，并通过既有状态 detour 记录和区分状态来源。
4. 由运行时 DLL 导出版本化的外部播放状态，在游戏线程调用已确认的暂停/恢复语义入口。

## 当前确认

2026-07-27 当前 IDA 数据库与最新运行日志确认：

```text
MusicRuntime_SetPlayStateAndNotify = 0x140C16690
RVA                                  0xC16690
```

旧文档中的 `0x140C15030` 只适用于此前数据库；该地址在当前数据库中位于
`sub_140C141E0` 内部，不再是独立函数入口。当前 ASI 运行日志记录的
`play state monitor installed at rva=0xC16690` 与 IDA 函数入口一致。

该函数职责：

```text
oldState = *(musicRuntime + 0x1910)
if oldState != newState:
  *(musicRuntime + 0x1910) = newState
  如果 newState == 0，清理队列/runtime 对象
  分发播放状态相关 fact/listener
  调用可选全局回调
```

相关字段：

```text
musicRuntime + 0x1910 : playState
musicRuntime + 0x1918 : currentRuntime
musicRuntime + 0x1924 : currentTrackId
```

## 2026-07-27 当前数据库静态复核

当前数据库已确认并命名以下函数：

```text
0x140C16690 MusicRuntime_SetPlayStateAndNotify
0x140C157C0 MusicRuntime_ApplyManualPause
0x140C15960 MusicRuntime_ResumeFromManualPause
0x140C15B90 MusicRuntime_UpdateAutoPauseBlock
0x1416F0C20 MusicPlayerUi_HandlePlayPauseAction
```

已确认签名：

```cpp
__int64 __fastcall MusicRuntime_SetPlayStateAndNotify(
    void* musicRuntime,
    unsigned __int8 newState);

void __fastcall MusicRuntime_ApplyManualPause(void* musicRuntime);
void __fastcall MusicRuntime_ResumeFromManualPause(void* musicRuntime);

void __fastcall MusicRuntime_UpdateAutoPauseBlock(
    void* musicRuntime,
    bool requestedBlock,
    unsigned __int16 blockBit);
```

`MusicRuntime_ApplyManualPause` 不是单纯写 `playState`：

- `currentRuntime = *(musicRuntime + 0x1918)` 必须非空；
- 只有 `playState == playing(1)` 时进入手动暂停路径；
- 它先清理并更新 current runtime object 的 `+0x2BA` 暂停计数；
- 必要时调用 current runtime object 的 `vtable + 0x118`；
- 随后发送曲目相关状态通知；
- 最后调用 `MusicRuntime_SetPlayStateAndNotify(..., paused(2))`。

`MusicRuntime_ResumeFromManualPause` 的静态边界：

- `playState == state3(3)` 时函数明确不执行恢复；
- `currentRuntime` 必须非空；
- `playState == paused(2)` 或内部 `state6(6)` 时进入恢复路径；
- 它撤销 current runtime object 的暂停计数和对应 vfunc 状态；
- 正常路径最后调用
  `MusicRuntime_SetPlayStateAndNotify(..., playing(1))`；
- 内部延迟重启标志生效时会回到 `state5(5)`，而不是直接进入
  `playing(1)`。

`MusicPlayerUi_HandlePlayPauseAction` 提供了上层调用约束的交叉确认：

- `musicRuntime + 0x1912` 的 auto-block mask 必须为零；
- 当前参与播放列表必须非空；
- `playState == playing(1)` 时调用
  `MusicRuntime_ApplyManualPause`；
- 当前条目有效且 `playState == paused(2)` 或内部 `state6(6)` 时调用
  `MusicRuntime_ResumeFromManualPause`；
- 其他状态会走播放/重建路径，不会强行调用恢复函数。

当前唯一入口签名：

```text
SetPlayState:
40 57 48 83 EC 20 0F B6 81 ? ? ? ? 48 8B F9 3A C2

ApplyManualPause:
48 89 5C 24 ? 57 48 83 EC 20 48 8B F9 48 8B 89 ? ? ? ?
48 85 C9 0F 85

ResumeFromManualPause:
40 57 48 83 EC 20 0F B6 81 ? ? ? ? 48 8B F9 3C 03

UpdateAutoPauseBlock:
48 89 5C 24 ? 55 56 57 48 83 EC 30 48 83 3D
```

## 2026-07-27 外部暂停状态链路核对

当前两种外部来源都已经产生明确的暂停状态：

- Spotify Web Playback SDK 的 `player_state_changed` 在曲目或暂停状态变化时
  发送带 `paused` 布尔值的 metadata；
- tabCapture 的页面适配器读取媒体元素或 Media Session 状态，并在 metadata
  中发送 `paused` 布尔值。

运行时 DLL 的当前行为：

- `AudioStreamClient` 只接受当前活跃音源的 metadata，非活跃来源会被忽略；
- `BrowserMetadata` 已缓存 `hasPausedState` 与 `paused`；
- 暂停状态仍用于给显示标题增加 `[PAUSED]` 前缀；
- `DS2AudioStreamReadPlaybackState` 同时导出状态版本、是否已知和
  `paused` 值；
- 只有 `known/paused` 实际发生变化时才递增非零版本号，重复 metadata
  不会产生重复控制事件。

当前外部状态链路：

```text
active source metadata.paused
  -> BrowserMetadata 已缓存
  -> DS2AudioStreamReadPlaybackState(version, known, paused)
  -> ExternalPlaybackStateSync 轮询并投递到游戏线程
  -> MusicRuntime_ApplyManualPause /
     MusicRuntime_ResumeFromManualPause
```

已注入的外部曲目：

```text
trackId = 0xAD900001
eventId = 0xAD100000
```

## ASI 监视点

新增模块：

```text
ds2_music_player_asi/PlayStateMonitor.cpp
ds2_music_player_asi/PlayStateMonitor.h
```

它通过当前函数开头签名定位 `MusicRuntime_SetPlayStateAndNotify`，安装 13 字节 x64 detour，并记录：

```text
oldState -> finalState
requested newState
currentTrackId
external=0/1
currentRuntime
callerRva
```

示例日志格式：

```text
music play state playing(1) -> paused(2) requested=paused(2)
trackId=0xAD900001 external=1 currentRuntime=... callerRva=...
```

## 2026-05-05 日志分析

样本来自游戏目录 `log.txt`，同一轮测试包含：

本节的 `callerRva` 和绝对地址属于 2026-05-05 当时的游戏版本，只用于保留
运行时语义证据；当前数据库地址以 2026-07-27 静态复核一节为准。

1. 进入特殊菜单，游戏自动停止音乐，离开菜单后恢复。
2. 手动暂停，再手动恢复播放。

外部曲目启动：

```text
idle(0) -> state5(5)  callerRva=0xC14073
state5(5) -> playing(1) callerRva=0xC13421
```

自动菜单暂停/恢复：

```text
playing(1) -> state3(3) callerRva=0xC1331A
state3(3) -> state4(4) callerRva=0xC146DE
state4(4) -> playing(1) callerRva=0xC134B8
```

结论：

- `state3` 不是手动暂停，而是游戏内部自动 pause/block 状态。
- `state4` 是自动 pause/block 解除后的恢复过渡状态。
- `MusicRuntime_UpdateAutoPauseBlock` 会维护 `musicRuntime+0x1912` 的 block bit mask。
- block 生效时，它把当前状态保存到 `musicRuntime+0x2824`，再设置 `playState=3`。
- block 全部清除且当前为 `state3` 时，它设置 `playState=4`。
- 主更新函数随后在 `state4` 路径里从 `musicRuntime+0x2824` 恢复旧状态；本样本恢复为 `playing(1)`。

手动暂停/恢复：

```text
playing(1) -> paused(2) callerRva=0xC142E4
paused(2) -> playing(1) callerRva=0x16E7CC1
```

结论：

- `paused(2)` 对应用户播放/暂停操作造成的手动暂停。
- 当前数据库中对应的手动暂停语义入口为
  `MusicRuntime_ApplyManualPause`，它会操作 current runtime object 后设置
  `playState=2`。
- 当时的手动恢复来自 UI 上层路径 `0x1416E7C10`；当前数据库中同语义的
  上层分流函数为 `MusicPlayerUi_HandlePlayPauseAction`
  (`0x1416F0C20`)，最终进入
  `MusicRuntime_ResumeFromManualPause` 并设置 `playState=1`。

## 游戏控制浏览器的现有语义

对外部音频流而言，游戏暂停源头应按状态区分：

```text
playing(1) -> state3(3): 浏览器 pause，原因=auto_block
state4(4) -> playing(1): 如果原因=auto_block，则浏览器 resume

playing(1) -> paused(2): 浏览器 pause，原因=manual
paused(2) -> playing(1): 如果原因=manual，则浏览器 resume
```

不要只看“非 1 就 pause，回到 1 就 resume”，因为 `state5` 是启动过渡，`state0` 是停止/清理，和暂停语义不同。

## 已实现控制链路

当前实现：

```text
PlayStateMonitor detour
  -> DS2AudioStreamSendBrowserControl(json)
  -> runtime DLL WebSocket text frame
  -> offscreen.js
  -> service_worker.js
  -> chrome.scripting.executeScript()
  -> target tab audio/video pause or play
```

runtime DLL 导出：

```text
DS2AudioStreamSendBrowserControl(const char* json)
```

浏览器扩展收到 `control` JSON 后，只控制当前捕获的 tab。暂停时会给被暂停的媒体元素标记 `data-ds2-paused-by-game`；恢复时只恢复带该标记的元素。

## 2026-07-27 已实现外部状态控制游戏

新增 ASI 模块：

```text
ds2_music_player_asi/ExternalPlaybackStatePolicy.h
ds2_music_player_asi/ExternalPlaybackStateSync.cpp
ds2_music_player_asi/ExternalPlaybackStateSync.h
```

状态采集与线程边界：

- 动态标题同步线程每 100 ms 读取一次版本化播放状态；
- 标题和艺术家读取仍保持 1000 ms 间隔，不因状态同步提高刷新开销；
- 版本和状态打包在同一个原子快照中，避免工作线程与游戏线程组合出
  不同版本的数据；
- 状态变化通过现有 `GameThreadDispatcher` 投递；
- 实际调用 `MusicRuntime_ApplyManualPause` 或
  `MusicRuntime_ResumeFromManualPause` 只发生在游戏窗口线程。

当前决策边界：

```text
自定义曲目 + playing(1) + 外部 paused
  -> MusicRuntime_ApplyManualPause

自定义曲目 + paused(2) + 外部 playing
  -> MusicRuntime_ResumeFromManualPause

state3/state4 或 auto-block mask 非零
  -> 不覆盖游戏自动暂停流程

state5 + 外部 paused，或 currentRuntime 尚未建立
  -> 保留该版本，等待可安全应用

非自定义曲目、未知外部状态、状态已经一致
  -> 消费该版本但不调用游戏控制函数
```

回声抑制：

- 外部状态正在调用游戏语义入口时，`PlayStateMonitor` 仍记录最终状态，
  但不再把同一变化发回外部；
- 外部暂停造成的 `playing(1) -> paused(2)` 会记录为
  `BrowserPauseReason::External`；
- 用户随后从游戏侧恢复时，仍会向当前外部音源发送 `resume`；
- 游戏自身的手动暂停、自动暂停及其恢复控制路径保持原有行为。

离线验证结果：

- `ExternalPlaybackStatePolicy` 用编译期断言覆盖暂停、恢复、auto-block、
  state5、非自定义曲目和未知状态边界；
- `test_source_arbitration.ps1` 已通过真实 WebSocket metadata 验证
  `paused=true -> paused=false` 的导出值和版本递增；
- 运行时 DLL、Base ASI、Spotify ASI 均构建成功；
- 上述结果确认导出、决策和编译链路；尚未把游戏内实测结果写成已确认事实。
