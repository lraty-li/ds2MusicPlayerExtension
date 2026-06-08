# 新会话交接：自定义曲目专辑图

本文档用于开启新会话时快速接手，不需要重新推导已确认事实。

## 项目目标

唯一目标：让 DS2MusicPlayer 注入的外部曲目显示正确的自定义专辑图，最终应能加载自定义 PNG。

音频播放、浏览器同步、外部曲目注入已经稳定；后续不要偏离专辑图方向。

## 必读规则

- 中文交流。
- 文件按 UTF-8 读取。
- 发生代码更改后执行 `ds2_music_player_asi\build.ps1`，构建失败时再单独拉详细错误。
- 任意单个代码文件不得超过 300 行。
- 不要并发调用 IDA MCP。
- 不准使用 `survey_binary`、`find_regex`、`find`、`search_text`，也不要用全局查找工具。
- 不要往知识文档写未经验证的结论。

## 启动新会话提示词

复制下面这段作为新会话开场：

```text
你在 E:\dev\code\game\DS2MusicPlayer 继续接手 DS2MusicPlayer 的专辑图逆向任务。中文交流。

先阅读 README.md、CLAUDE.md、AGENTS.md、RE\Handoff_20260607_NewSessionPrompt.md、RE\Handoff_20260607_AlbumJacket.md、RE\ConfirmedRuntimeFindings.md，理解项目目标与已确认事实。读取文件用 UTF-8。不要使用 survey_binary、find_regex、find、search_text，也不要用全局查找工具；不要并发调用 IDA MCP。

唯一目标是让外部/自定义曲目显示正确的自定义专辑图，最终加载 PNG。音频播放已经稳定，不要改动无关功能。

当前最新状态：19:09 日志确认当前构建已经回到自有 VirtualAlloc pixelBuffer 拷贝路径，用户仍看到苹果图。原因已经确认：clone 的 +0x88/+0xD8 已重定位到 clone，且 clone 内没有残留指向 HotSpring PB 的引用，但 clone 的 +0x90/+0xE0 仍然等于 HotSpring 原生 descriptor/resource blocks。此前 NO DATA descriptor 控制实验已证明 +0x90/+0xE0 会主导可见图像。

下一步不要继续扩展 DXBC payload patch，也不要继续让用户反复开关游戏做盲测。IDA 已确认 TextureDX12 的 +0x90/+0xE0 由 sub_1420F34E0 分配 descriptor block，并由 sub_142117000 调用全局 GPU/D3D 接口 vtable+0x90 写入 view；但 19:09 日志进一步证明 clone 的 f88 wrapper+0x8 真实 GPU resource object 仍等于 HotSpring，只有 wrapper+0x30 CPU data 指向 clone。因此仅调用/复现 sub_142117000 只能新建 view，仍会引用 HotSpring GPU resource。下一步应让 clone 拥有自有 engine GPU resource object，然后走 TextureDX12_bind_resource_handle_create_views / TextureDX12_upload_texture_payload 这类真实资源绑定和上传路径。发生代码更改后运行 ds2_music_player_asi\build.ps1，构建成功后再通知我启动游戏。
```

## 最新日志解释

最新一轮 `log.txt` 时间为 2026-06-07 19:09 至 19:13。

关键日志：

```text
uiclone PB copy: srcPB=0x41990420000 newPB=0x1D068EC0000 size=655360 relocated=321 patchedPages=5
pbcmp begin hot=0x41990420000 hotSize=655360 noData=0x4198EF9EE00 noDataSize=4985344 clone=0x1D068EC0000 cloneSize=655360
clone +0x88 = 0x1D068ED0000
clone +0xD8 = 0x1D068ED0000
clone +0x90 = 0x1D00171A430
clone +0xE0 = 0x1D00171A450
hot   +0x90 = 0x1D00171A430
hot   +0xE0 = 0x1D00171A450
noData+0x90 = 0x1D0016E9610
noData+0xE0 = 0x1D0016E9630
pbcmp clone refs-to-hot aligned=0 any=0
uiclone OK: srcTexture=0x41990410000 newTexture=0x1D030713680
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

用户报告游戏内仍显示苹果图。当前解释：

- 起始 slot 使用 `HotSpringImageTextures[0]`，该图用户看到过“苹果”。
- 当前实验克隆 HotSpring 的 UITexture/Texture/pixelBuffer，并 patch CPU DXBC payload。
- clone 的 `+0x88/+0xD8` 已指向 clone 内存，说明部分内部页/包装器重定位成功。
- clone 的 `+0x90/+0xE0` 仍指向 HotSpring descriptor/resource blocks。
- 因此显示苹果，说明可见图像仍跟随 HotSpring GPU descriptor/resource 状态。

## 已确认对象链

```text
Track+0x50
  -> StreamingRef slot { target, packed }
    -> target+0x20 loaded UITexture
      -> UITexture+0x30 Texture
        -> Texture+0x20 pixelBuffer
```

已确认事实：

- 替换 `Track+0x50` 的 slot 会改变自定义曲目专辑图。
- `UITexture+0x30` 是可见 Texture 指针。
- `Texture+0x20` 是可见 pixelBuffer 指针。
- `Texture+0x20` 指向另一个引擎原生 pixelBuffer 时，画面会跟随那个 pixelBuffer。
- `Texture+0x20` 根对象的 `+0x90/+0xE0` descriptor/resource blocks 是当前可见图像的主导字段。
- 自有对象链稳定：自建 target、克隆 UITexture、克隆 Texture、VirtualAlloc pixelBuffer 副本不会立刻崩溃。
- 播放路径稳定：`state5(5) -> playing(1)`，暂停也正常。

## 视觉实验矩阵

| 实验 | 结果 | 结论 |
|------|------|------|
| `DefaultConstructionHoloImageTexture` | `NO DATA` | 这是可用的控制样本 |
| `HotSpringImageTextures[0]` | 苹果图 | 这是可用的游戏图片控制样本 |
| HotSpring UITexture 的 `+0x30` 改为 NO DATA 原生 Texture | `NO DATA` | 视觉由 `UITexture+0x30` 控制 |
| HotSpring clone Texture 的 `+0x20` 改为 NO DATA 原生 pixelBuffer | `NO DATA` | 视觉由 `Texture+0x20` 控制 |
| VirtualAlloc 拷贝 HotSpring pixelBuffer 并补丁 payload | 仍显示苹果 | 自有 pixelBuffer 拷贝不等价于引擎原生 pixelBuffer |
| clone PB 的 `+0x90/+0xE0` 仍保留 HotSpring descriptor | 苹果图 | 可见图像跟随 GPU descriptor/resource blocks |

## 当前代码状态

重点文件：

- `ds2_music_player_asi\CustomJacketPixelTest.cpp`
  - `ProbeThread` 当前已回到自有 pixelBuffer 拷贝路径。
  - 最新显示苹果图的原因是 clone 仍继承 HotSpring 的 `+0x90/+0xE0`。
- `ds2_music_player_asi\CustomJacketAltTextureProbe.cpp`
  - 预加载 `DefaultConstructionHoloImageTexture`。
  - 提供 `TryCloneAlternatePixelBufferToTrack`。
- `ds2_music_player_asi\CustomJacketTextureOverrideProbe.cpp`
  - 克隆 Texture header，并把 `Texture+0x20` 写成指定 pixelBuffer。
- `ds2_music_player_asi\CustomJacketUiClone.cpp`
  - 自有 UITexture/Texture 克隆路径。
- `ds2_music_player_asi\CustomJacketPixelBufferClone.cpp`
  - VirtualAlloc 拷贝 pixelBuffer、内部 qword 指针重定位、DXBC payload patch。
- `RE\Handoff_20260607_AlbumJacket.md`
  - 包含当天完整实验历史。
- `RE\ConfirmedRuntimeFindings.md`
  - 包含确认后的结构事实。

上次已知构建成功，19:09 日志确认新 ASI 已运行。

## 2026-06-07 19:57 最新更新

19:09 日志里 `pbres wrapper` 已经确认：

```text
hot.f88+0x8    = 0x1D04DDE40F0
noData.f88+0x8 = 0x1D03ADC02D0
clone.f88+0x8  = 0x1D04DDE40F0

hot.f88+0x30    = 0x41990420018
noData.f88+0x30 = 0x4198EF9EE18
clone.f88+0x30  = 0x1D068EC0018
```

clone 的 CPU data 已经指向自有 patched buffer，但真实 GPU resource/ref
object 仍然等于 HotSpring。因此仍显示苹果图的根因已经从
`+0x90/+0xE0` descriptor blocks 进一步缩小到 engine GPU resource object
仍继承 HotSpring。

IDA 数据库已重命名并注释：

- `TextureDX12_bind_resource_handle_create_views` (`0x142116B40`)
- `TextureDX12_clone_resource_handle_create_view` (`0x142117000`)
- `TextureDX12_create_srv_uav_descriptors` (`0x142118A40`)
- `D3DResourceManager_create_resource_wrapper` (`0x140D18D20`)
- `D3DResourceManager_create_placed_resource` (`0x140D19170`)
- `TextureDX12_upload_texture_payload` (`0x142113810`)

本轮代码只新增诊断：`CustomJacketPixelBufferGpuResource.cpp` 会打印
`pbres link f88 ... cloneResEqHot=...` 和 resource object vtable 槽位。
`ds2_music_player_asi\build.ps1` 已在 2026-06-07 19:57 成功执行，输出
`BUILD_OK`。

## 2026-06-07 20:00 运行日志结论

新构建已运行，关键日志：

```text
hot.wrapper=0x54670030000 hot.resource=0x1FA8E3C9D00 hot.cpu=0x54670020018
noData.wrapper=0x5466EB86D80 noData.resource=0x1FA7D16C2C0 noData.cpu=0x5466EB4EE18
clone.wrapper=0x1F5B2EE0000 clone.resource=0x1FA8E3C9D00 clone.cpu=0x1F5B2ED0018
cloneResEqHot=1 cloneCpuEqHot=0
```

结论：clone 只把 wrapper 和 CPU data 指针换成了自有对象，但真实
GPU resource pointer 仍等于 HotSpring。NO DATA 的 resource pointer 不同。
三者 resource vtable 相同，说明问题不是 vtable 类型，而是 clone 没有创建
或上传到自有 GPU resource。

## 下一步工程动作

新会话应继续工程验证，目标是让 clone `TextureDX12` / pixelBuffer
拥有自有 engine GPU resource object，并完成上传和 view 创建，而不是
继承 HotSpring 的 resource object 或 descriptor blocks。

需要做的具体动作：

1. 保留 `altpb` 控制函数，不删除；它是 NO DATA descriptor 控制样本。
2. 不要继续扩大 CPU DXBC payload patch。
3. IDA 已确认 `sub_142112E30` 是 `TextureDX12` 构造函数，`sub_142113000` 是释放路径。
4. IDA 已确认 `sub_142117000(dstTextureDX12, srcTextureDX12)` 会通过 `sub_1420F2CF0` 复制 24 字节 descriptor handle，通过 `sub_1420F34E0(qword_14623FB38 + 8818080)` 分配 `+0x90/+0xE0` descriptor block，并调用全局 GPU/D3D 接口 `xmmword_1463E0CB0` vtable `+0x90` 写入 view，但该路径仍引用源 GPU resource。
5. IDA 已确认 `0x140D18D20` / `0x140D19170` 是 D3D resource wrapper / placed resource 创建路径，`0x142113810` 是 TextureDX12 payload upload 路径。
6. 下一步优先围绕自有 engine GPU resource object 创建和上传做诊断，而不是只新建 descriptor view。
7. 代码更改后执行 `ds2_music_player_asi\build.ps1`，构建成功后再通知用户启动游戏。

## 当前判断

问题已经缩小到 pixelBuffer 内部状态，而不是：

- `Track+0x50` slot 选择错误。
- StreamingRef assign 失败。
- `UITexture+0x30` 偏移错误。
- `Texture+0x20` 偏移错误。
- 外部曲目播放状态错误。

更可能的差异包括：

- pixelBuffer 内存在非 8 字节对齐指针或未重定位引用。
- pixelBuffer 根对象包含 GPU 资源句柄、资源注册表节点或 allocator 私有状态。
- VirtualAlloc 内存区域类型、保护属性或对象登记状态与引擎 heap 分配不同。
- 当前 patch 改了 CPU 副本中的 DXBC payload，但可见图像仍来自 `+0x90/+0xE0` 指向的 HotSpring GPU descriptor/resource blocks。

`+0x90/+0xE0` 主导可见图像已经由日志和控制实验确认；其创建/更新路径已定位到 `sub_142117000` / `sub_1420F34E0` / GPU 接口 vtable `+0x90`。
