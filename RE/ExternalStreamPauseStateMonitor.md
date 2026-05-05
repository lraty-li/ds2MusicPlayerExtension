# 外部音频暂停/恢复状态监视

## 目标

确认游戏内音乐播放器何时把当前曲目切到暂停或恢复播放，为后续把外部浏览器音源同步暂停/恢复做准备。

## 方法

1. 查阅 archive 中旧的播放/暂停探索记录。
2. 用 IDA 当前数据库重新确认旧地址对应关系，不直接沿用旧地址。
3. 在 ASI 中加入运行时模式扫描和 detour，只记录状态变化，不改变游戏逻辑。

## 当前确认

旧记录中的 `sub_140C147B0` 在当前数据库中对应：

```text
MusicRuntime_SetPlayStateAndNotify = 0x140C15030
```

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

## 后续验证

需要在游戏内分别触发：

1. 外部曲目播放中手动暂停。
2. 外部曲目播放中由游戏系统自动暂停。
3. 自动暂停后的自动恢复。

对比日志中的 `callerRva`，即可判断自动暂停/恢复是否汇聚到同一状态函数，以及是否需要额外 hook 更上层的暂停原因来源。

## 2026-05-05 日志分析

样本来自游戏目录 `log.txt`，同一轮测试包含：

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
- 手动暂停路径位于 `MusicRuntime_ToggleManualPause`，它会操作当前 runtime object 后设置 `playState=2`。
- 手动恢复来自 UI 上层路径 `0x1416E7C10`，最终进入 `MusicRuntime_ResumeFromManualPause` 并设置 `playState=1`。

## 控制浏览器的建议语义

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
