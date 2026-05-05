# OnPressOptions-播放暂停探索

## 目标

围绕 `OnPressOptions -> 播放/暂停` 这条链持续记录新发现。

## 已命中事实

- `OnPressOptions` 实际落到 `DSUIMusicMenu_OnPressOptions_Thunk`
- `DSUIMusicMenu_OnPressOptions_Thunk` 直接跳到 `DSUIMusicMenu_HandleTogglePlayback`
- `播放中按暂停` 会命中 `DSUIMusicMenu_HandleTogglePlayback` 内部 `g_MusicRuntime->playState == 1` 这一支
- `播放中按暂停` 时，`DSUIMusicMenu_HandleTogglePlayback` 的第一真实处理函数是 `0x140C138D0`

## 类型文件

- 类型声明单独放在 [OnPressOptions-播放暂停探索.c](E:\dev\code\game\DS2MusicPlayer\notes\OnPressOptions-播放暂停探索.c)

## 当前函数观察

### 0x140C138D0

- 形参是 `g_MusicRuntime`
- 开头先读：

```cpp
g_MusicRuntime->currentPlayerObj   // +0x1918
```

- 若当前播放器存在且 `playState == 1`，会：

```cpp
currentPlayerObj->counter2BA = 0;
currentPlayerObj->vtbl[0x118/8](currentPlayerObj, 1, 0);
++currentPlayerObj->counter2BA;
sub_140C80360(currentTrackId, sub_140C15DA0(g_MusicRuntime));
sub_140C164A0(g_MusicRuntime, sub_140C15DA0(g_MusicRuntime), currentTrackId, 1);
sub_140C147B0(g_MusicRuntime, 2);
```

- 若当前播放器不存在，则会清理：

```cpp
currentTrackId
lastTrackId
state2824
```

并走：

```cpp
sub_140C147B0(g_MusicRuntime, 0);
sub_140C15E00(g_MusicRuntime);
```

### 0x140C147B0

- 该函数直接写：

```cpp
g_MusicRuntime->playState = newState;   // +0x1910
```

- `播放中按暂停` 这条已命中链会从 `0x140C138D0` 以：

```cpp
sub_140C147B0(g_MusicRuntime, 2);
```

进入这里

- 若 `newState == 0`，函数会遍历：

```cpp
g_MusicRuntime + 0x2898   // 条目数组
g_MusicRuntime + 0x28A4   // 条目数量
```

每项大小为 `0x38`，并处理其中：

```cpp
entry + 0x18
entry + 0x20
entry + 0x30
```

其中 `entry+0x18` 和 `entry+0x20` 都像播放器对象指针，`entry+0x30` 像启用/状态整数字段。

- 在 `newState == 0` 的清理路径里，若 `entry+0x18` 或 `entry+0x20` 非空，会：

```cpp
obj->flags180 &= ~4;
obj->vtbl[0x110/8](obj, 0);
release(obj);
entryPtr = 0;
```

- 在写完状态后，函数还会把新状态分发到 `qword_14622ED88` 相关的三个回调槽位。

## 静态候选

- `DSUIMusicMenu_HandleTogglePlayback` 内部读取的 `g_MusicRuntime + 0x1910` 是播放状态字节
- `playState == 1` 分支更像暂停路径
- `playState != 1` 分支更像恢复/启动路径
- `0x140C138D0` 更像 `暂停当前播放` 处理函数
- `0x140C147B0` 更像 `MusicRuntime_SetPlayState`

## 明确反证

- 暂无

## 待继续确认

- `播放中按暂停` 时，`0x7FF619FDBB4C` 调到的第一真实处理函数
- `playState != 1` 这一支是否就是恢复播放主链
- 该函数链是否会继续落到音乐运行时对象，而不是仅停在 UI/请求层
- `g_MusicRuntime + 0x2898` 这张 `0x38` 条目表的完整结构
