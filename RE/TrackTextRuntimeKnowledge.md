# 曲目信息文本实时刷新知识库

本文只记录 DS2 音乐播放器曲名、艺术家等文本信息实时替换相关的已验证知识。
不包含插件实现细节。

## 目标

外部/自定义曲目在浏览器切换音乐时，游戏内显示的曲名和艺术家信息应能实时更新。

2026-06-10 已验证：

- 浏览器切换音乐后，游戏内曲目信息可以实时替换。
- 不需要关闭并重新打开音乐菜单才能看到新曲名。
- 该机制与专辑图实时替换类似，都可以绕过菜单重建等待；但两者底层路径不同。

## 文本资源层

自定义曲目的基础文本仍来自 `Track` / `Album` 资源对象。

已确认字段：

```text
Track+0x38: Track title LocalizedText*
Album+0x30: Album artist LocalizedText*
Album+0x40: Telop artist LocalizedText*
```

`LocalizedText` 已确认字段：

```text
LocalizedText+0x20: char* text buffer
LocalizedText+0x28: uint16 text length
```

只改这些 `LocalizedText` 字段可以改变下一次菜单重建时读取到的文本。
但在当前版本中，仅改资源层文本并不保证当前已显示 UI 立即更新。

## UI Entry 缓存层

音乐菜单会把资源层文本转换为 UI 使用的 shared-string，并缓存到
`MusicRuntime` 的 entry 数组中。

已确认 `MusicRuntime` 字段：

```text
MusicRuntime+0x1938: entry count
MusicRuntime+0x1940: entry array pointer
entry stride:        0x38
```

已确认 entry 字段：

```text
entry+0x00: cached title UI shared-string slot
entry+0x08: cached artist UI shared-string slot
entry+0x10: TrackResource*
entry+0x18: AlbumResource*
entry+0x28: copy of Track+0x24
entry+0x34: flag
```

自定义曲目 entry 可通过 `entry+0x10` 指向的 `TrackResource*` 识别。
当前自定义曲目的 `Track+0x20` id 为：

```text
0xAD900001
```

## 菜单重建路径

已确认函数：

| 地址 | 名称 | 已确认职责 |
| --- | --- | --- |
| `0x140C124D0` | `MusicRuntime_RebuildTrackEntriesFromResources` | 从资源对象重建 `MusicRuntime` entry 数组 |
| `0x1427023A0` | `LocalizedText_ToUiSharedString` | 从 `LocalizedText` 生成 UI shared-string slot |
| `0x1400A3920` | `UiSharedString_MoveAssignSlot` | 释放旧 slot，并把新 slot 移动赋值到目标 |
| `0x1400A37A0` | `UiSharedString_ReleaseSlot` | 释放临时 shared-string slot |

`MusicRuntime_RebuildTrackEntriesFromResources` 重建 entry 时已确认行为：

- 读取 `Track+0x38`，转换为 title shared-string，写入 `entry+0x00`。
- 读取 `Album+0x30`，转换为 artist shared-string，写入 `entry+0x08`。
- 保存 `TrackResource*` 到 `entry+0x10`。
- 保存 `AlbumResource*` 到 `entry+0x18`。
- 将 rebuilt entry array 发布到 `MusicRuntime+0x1940`，并更新
  `MusicRuntime+0x1938` count。

因此，开关菜单时文本会更新，是因为菜单重建路径重新读取了资源层
`LocalizedText` 并生成了新的 entry 缓存字符串。

## 实时刷新路径

当前已验证的实时刷新关键点：

```text
资源层 LocalizedText 更新
  -> 用 LocalizedText_ToUiSharedString 生成新的 UI shared-string slot
  -> 用 UiSharedString_MoveAssignSlot 替换当前 entry+0x00 / entry+0x08
  -> 当前 UI 可见文本实时变化
```

这说明曲名/艺术家实时替换的核心不是触发完整菜单重建，而是刷新当前
`MusicRuntime` entry 中已经缓存的 UI shared-string slot。

该路径与专辑图的差异：

- 专辑图 entry 保留 `TrackResource*`，可见图像继续沿
  `Track+0x50 -> UITexture -> Texture -> TextureDX12` 链路读取。
- 文本 entry 保存的是转换后的 shared-string slot，不是持续读取
  `LocalizedText` 指针。
- 因此专辑图可以通过替换资源链实时变化；文本需要额外刷新 entry
  的 cached title / artist slot。

## Shared-String 行为

`LocalizedText_ToUiSharedString` 已确认行为：

- 读取 `LocalizedText+0x20` 的文本指针。
- 读取 `LocalizedText+0x28` 的文本长度。
- 根据文本内容构造 UI shared-string slot。
- 文本为空时走默认字符串回退路径。

`UiSharedString_MoveAssignSlot` 已确认行为：

- 若目标 slot 与源 slot 不同，会释放目标旧 slot。
- 将源 slot 指针写入目标 slot。
- 将源 slot 重置为空字符串单例。

这表明直接替换 `entry+0x00` / `entry+0x08` 时，应使用游戏自身的
shared-string 构造与 move-assign 生命周期路径，而不是裸写字符串指针。

## 已验证结论

- 曲名和艺术家信息可以像专辑图一样达到实时替换效果。
- 实时替换不是因为 UI 每帧重新读取 `LocalizedText`。
- 当前版本中，UI 在菜单重建时会重新读取资源层文本；实时刷新需要更新
  `MusicRuntime` entry 缓存层。
- `Track+0x38`、`Album+0x30`、`Album+0x40` 仍是基础文本来源。
- 当前可见 entry 的 `entry+0x00` 和 `entry+0x08` 是实时刷新曲名/艺术家的
  关键缓存位点。
- 使用原生 shared-string 构造/赋值路径刷新 entry 后，浏览器换歌可在游戏内
  实时反映。

## 已排除项

- 不是音频播放状态导致曲名刷新。
- 不是浏览器 metadata 无法到达游戏进程。
- 不是单纯写 `LocalizedText+0x20/+0x28` 就一定能让当前 UI 实时刷新。
- 不是必须完整调用 `MusicRuntime_RebuildTrackEntriesFromResources` 才能刷新当前文本。
- 不是专辑图与文本共用同一条 UI 数据路径；两者实时生效点不同。
