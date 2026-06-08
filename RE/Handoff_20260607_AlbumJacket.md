# 专辑图功能交接文档

## 日期

2026-06-07

## 目标回顾

让 DS2 外部音源自定义曲目显示**不同颜色的测试专辑图**（最终目标：加载 PNG 自定义图片）。

## 当前状态

当前主线已进入 **self-owned clone 链验证**：自定义曲目使用
`HotSpringImageTextures[0]` 作为源图，随后构造自有 target、cloned
`UITexture`、cloned `Texture` 和 `VirtualAlloc` pixelBuffer 副本。
目前不会修改游戏原始 `Texture` 或原始 pixelBuffer。

## 2026-06-07 更新：NO DATA 占位图验证

最新视觉测试确认：将自定义曲目的 `Track+0x50` 指向
`DSUICatalogueImageResource+0xC8`
`DefaultConstructionHoloImageTexture` 后，游戏内显示了不同于默认音乐 jacket 的图片。
该图片是系统 `NO DATA` 占位图，日志对应：

```
custom jacket applied: source=DefaultConstructionHoloImageTexture offset=0xc8
jres ui d+0x20: 0x0 0x100 0xa0 0x0
```

确认结论：

- `Track+0x50` 的 `StreamingRef_UITexture` slot 替换会直接影响音乐菜单可见专辑图。
- `DefaultConstructionHoloImageTexture` 本身有效，但视觉内容是占位图，不适合作为最终来源。
- 后续应继续寻找非占位的 catalogue `StreamingRef_UITexture` 候选，或构造独立 `UITexture`；不应回到直接覆写 `Texture+0x20` 页表路线。

## 2026-06-07 更新：HotSpring 数组候选验证

下一版代码改为优先从 `Array_StreamingRef_UITexture` 里选择候选，日志确认本轮选中：

```
custom jacket applied: source=HotSpringImageTextures offset=0x50 index=0
```

用户观察到游戏中显示了非 `NO DATA` 的图片，内容像“几个苹果”。同一轮日志确认外部曲目播放稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

确认结论：

- catalogue 数组里的 `StreamingRef_UITexture` 条目可直接写入 `Track+0x50`，并会显示对应素材。
- `HotSpringImageTextures[0]` 是一个真实游戏素材图，而不是系统占位图。
- 这进一步证明最终目标应转向“构造一个合法 UITexture slot/loaded object”，而不是继续寻找音乐菜单里的额外哈希字段。

## 2026-06-07 更新：自建 target + cloned UITexture 验证

下一版在 `HotSpringImageTextures[0]` 源图加载后执行：

1. 克隆 loaded `UITexture` 外壳。
2. 复用原 `Texture` 指针，不改 `Texture+0x20`。
3. 自建新的 StreamingRef target。
4. 通过 context vtable[3] `assign_loaded` 把 `Track+0x50` 切到新 target。

日志确认：

```
uiclone OK: srcSlot=0x17EE381C4F0 srcTarget=0x41AB02B35A0 newTarget=0x17ECE5EEF00 newUI=0x183F18705B0 texture=0x41AB1810000
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

确认结论：

- `Track+0x50` 可以稳定承载我们自己分配的 target。
- loaded object 也可以是我们自己分配并修正 header 的 cloned `UITexture`。
- 当前仍复用源 `Texture`，所以视觉应保持苹果图；下一步应替换 `newUI+0x30` 指向的 `Texture`，而不是再验证 slot/target 路线。

## 2026-06-07 更新：cloned Texture 验证

下一版把 `newUI+0x30` 改为指向我们新分配的 cloned `Texture`：

- 只复制源 `Texture` 前 `0x70` 字节头部。
- `Texture+0x20` 仍复用源 pixelBuffer。
- 修正内部链为新对象内部地址：
  `+0x70 -> +0xE0 -> +0x150 -> +0x1C0 -> 0`。

日志确认：

```
uiclone OK: srcTexture=0x36236010000 newTexture=0x1F93093BEB0
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

确认结论：

- 自建 target + cloned `UITexture` + cloned `Texture` 对象链稳定。
- 目前视觉仍应是苹果图，因为 pixelBuffer 仍来自源素材。
- 下一步应验证能否替换 cloned `Texture+0x20` 指针为我们分配的 pixelBuffer 结构；如果这一步失败，应回到资源层 `TextureInfo` / `UITextureInfo` 构造，而不是修改原对象。

## 2026-06-07 更新：自分配 pixelBuffer 副本验证

下一版让 cloned `Texture+0x20` 指向我们自己的 `VirtualAlloc` buffer：

1. 读取源 `Texture+0x20`。
2. `VirtualQuery` 获取当前可读范围，本轮为 655360 字节。
3. `VirtualAlloc` 新 buffer 并复制源 pixelBuffer。
4. 扫描 qword，把落在源 pixelBuffer 范围内的内部指针重定位到新 buffer。
5. cloned `Texture+0x20` 指向新 buffer。

日志确认：

```
uiclone PB copy: srcPB=0x3D876C20000 newPB=0x28EF03C0000 size=655360 relocated=321
uiclone OK: newTexture=0x28EF206C950
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
music play state paused(2) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

确认结论：

- 自分配 pixelBuffer 副本可被 cloned `Texture` 稳定使用。
- 需要重定位源 pixelBuffer 内指向自身区域的 qword 指针；本轮重定位 321 个。
- 现在对象链已完全由我们控制，下一步可以在副本上做最小可见像素实验，仍应避免修改原对象。

## 2026-06-07 更新：小范围 DXBC payload 补丁未改变视觉

下一版在自有 pixelBuffer 副本中，对每个解析到的 DXBC 页只补丁第一个
mip payload 的前 256 字节。日志确认补丁已执行：

```
custom jacket applied: source=HotSpringImageTextures offset=0x50 index=0
jres candidate tex+0x20=0x4910F820000 q0=0x7FF710C75C98 q1=0x2
uiclone PB patch: pages=5 bytes=1280
uiclone PB copy: srcPB=0x4910F820000 newPB=0x180C6360000 size=655360 relocated=321 patchedPages=5
uiclone OK: srcTexture=0x4910F810000 newTexture=0x18071FB0220
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

用户视觉观察仍是 HotSpring 源图（“苹果图”），不是彩色测试图。

确认结论：

- 自有 target / `UITexture` / `Texture` / pixelBuffer 副本链仍稳定。
- 小范围补丁命中了 5 个 DXBC 页并写入 1280 字节，但不足以改变可见图像。
- 下一轮应扩大到每个已解析 DXBC 页的完整 first-mip payload 区间，
  并记录每页 `off/marker/payload/dataSize/writeBytes`；若完整 payload
  仍不变，重点转向 GPU resource/cache 或资源层 `TextureInfo` 构造。

## 2026-06-07 更新：完整 first-mip payload 补丁仍未改变视觉

下一版把补丁扩大到每个解析到的 DXBC 页完整 first-mip payload 区间。
日志确认命中并写入：

```
uiclone PB patch page off=0x40000 marker=0x51 payload=0x8d dataSize=2528 writeBytes=2528 mips=7
uiclone PB patch page off=0x60000 marker=0x51 payload=0x8d dataSize=2528 writeBytes=2528 mips=7
uiclone PB patch page off=0x63000 marker=0x51 payload=0x8d dataSize=2528 writeBytes=2528 mips=7
uiclone PB patch page off=0x70000 marker=0x51 payload=0x8d dataSize=3144 writeBytes=3144 mips=7
uiclone PB patch page off=0x80000 marker=0x51 payload=0x8d dataSize=2528 writeBytes=2528 mips=7
uiclone PB patch: pages=5 bytes=13256
uiclone PB copy: srcPB=0x50487020000 newPB=0x26367650000 size=655360 relocated=321 patchedPages=5
uiclone OK: srcTexture=0x50487010000 newTexture=0x263307647E0
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

用户视觉观察仍是 HotSpring 源图（苹果图）。

确认结论：

- 不是“只写 256 字节太少”的问题；完整 first-mip payload 写入也不改变可见图。
- `Texture+0x20` 指向的自有 pixelBuffer 副本足以保持对象链稳定，
  但当前 UI 显示可能已经走 GPU/cache 资源，或读取 `UITexture` 的其它
  runtime 指针，而不是重新解析 `Texture+0x20` 的 CPU 页数据。
- 下一轮控制实验应加载一张已知不同的 `UITexture`（例如
  `DefaultConstructionHoloImageTexture` / NO DATA），只把 cloned
  `UITexture+0x30` 指向它的 `Texture`。若画面变成 NO DATA，说明
  `+0x30` 的 Texture 指针控制显示；若仍是苹果图，说明显示路径依赖
  `UITexture` 附加缓存字段。

## 2026-06-07 更新：`UITexture+0x30` Texture 指针控制实验成功

下一版预加载 `DefaultConstructionHoloImageTexture`，然后克隆当前
HotSpring 源图的 `UITexture`，但把 cloned `UITexture+0x30` 指向
`DefaultConstructionHoloImageTexture` 已加载出来的 `Texture`。
日志确认：

```
uiclone alt prepared: source=DefaultConstructionHoloImageTexture target=0x546D39128B0
custom jacket applied: source=HotSpringImageTextures offset=0x50 index=0
uiclone alt loaded: loadedUI=0x2AC4289BC98 texture=0x546D3B7C7F0
uiclone alt OK: label=DefaultConstructionHoloImageTexture srcTexture=0x546D5010000 overrideTexture=0x546D3B7C7F0
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

用户视觉观察变成 `NO DATA`。

确认结论：

- `UITexture+0x30` 是音乐菜单可见图的直接控制入口。
- 显示路径不是固定读取 HotSpring 源 `UITexture` 的其它附加字段；
  cloned `UITexture` 保留源对象其它 runtime/cache 指针时，只改 `+0x30`
  就足以改变可见图。
- 之前 cloned `Texture + 自有 pixelBuffer 副本` 仍显示苹果图，问题应集中在
  `Texture` / `Texture+0x20` / GPU resource 绑定层，而不是
  `StreamingRef target` 或 `UITexture` 层。
- 下一轮应做 `Texture+0x20` 指针控制实验：克隆 HotSpring `Texture`
  头部，但把 cloned `Texture+0x20` 指向 NO DATA 原始 `Texture+0x20`
  的 engine-owned pixelBuffer。若画面变 NO DATA，说明显示跟
  `Texture+0x20` 根对象走，副本失败是因为 copied pixelBuffer 保留/缺失
  engine-owned 外部 GPU 状态；若仍是苹果图，说明 `Texture` 头部或其它
  GPU 绑定字段决定显示。

## 2026-06-07 更新：`Texture+0x20` engine-owned pixelBuffer 控制实验成功

下一版克隆 HotSpring 源 `Texture` 头部，但不使用 VirtualAlloc 副本，
而是把 cloned `Texture+0x20` 直接指向
`DefaultConstructionHoloImageTexture` 原始 `Texture+0x20`
engine-owned pixelBuffer。日志确认：

```
uiclone altpb loaded: loadedUI=0x26FC289BC98 texture=0x3A30277C7F0 pixelBuffer=0x3A30274EE00
uiclone altpb Texture: label=DefaultConstructionHoloImageTexture srcTexture=0x3A304410000 newTexture=0x273F29597C0 overridePB=0x3A30274EE00
uiclone altpb OK: label=DefaultConstructionHoloImageTexture srcTexture=0x3A304410000 newTexture=0x273F29597C0 overridePB=0x3A30274EE00
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

用户视觉观察变成 `NO DATA`。

确认结论：

- `Texture+0x20` 指向的 engine-owned pixelBuffer 根对象可以直接决定可见图。
- `Texture` 头部/GPU 绑定字段不是唯一控制因素；克隆 HotSpring
  `Texture` 头部后只换 `+0x20`，画面仍能跟随 NO DATA pixelBuffer。
- 之前自有 VirtualAlloc pixelBuffer 副本显示不变，说明“memcpy 根对象 +
  qword 内部指针重定位”并没有复制出等价的 engine-owned pixelBuffer。
  可能原因包括：根对象中存在外部 GPU/resource 句柄、页表指针没有全部
  重定位、内部指针并非只按 qword 对齐保存，或引擎只认可其 allocator /
  object registry 管理下的 pixelBuffer。
- 下一轮应定位 VirtualAlloc 副本与 engine-owned pixelBuffer 的差异：
  优先记录/比较 HotSpring 与 NO DATA 的 `Texture+0x20` 根对象头部、
  关键指针字段和 memory allocation 属性；不要继续扩大 raw payload patch。

## 2026-06-07 更新：探针重试与文件拆分

本次源码检查确认，13:41 日志中的：

```
bcn: failed to read PB info
```

来自 `CloneAndReplacePixelBuffer()` 的首次 pixel buffer 探测失败。旧实现中 `ProbeThread` 只会尝试一次，失败后 `attempted=true`，后续 tick 只继续打印 loaded，不再尝试替换，因此不会出现 `bcn OK`，游戏内也不会显示彩色测试图。

已完成修正：

- `CustomJacketPixelTest.cpp` 从 485 行拆分为多个小文件，所有单个 `.cpp/.h` 均低于 300 行。
- 新增 `CustomJacketInternal.h`、`CustomJacketSeh.cpp`、`CustomJacketBcn.cpp`、`CustomJacketClone.cpp`。
- `ProbeThread` 改为最多 30 秒内持续重试；只有 `CloneAndReplacePixelBuffer()` 返回成功后才停止。
- pixel buffer 探测改用 `VirtualQuery` 限制可读范围，避免一次越界读导致永久放弃。
- 成功构建并输出到游戏 `scripts\Ds2MusicPlayerExtend.asi`。

## 2026-06-07 更新：DXBC 未命中后的结构探针

13:56 新日志确认重试逻辑生效，但每次均为：

```
dxbcPages=0
bcn: no DXBC pages found
```

本轮新增 `CustomJacketProbe.cpp`，只在每次加载周期首次 DXBC 未命中时输出一次 `Texture+0x20` 指向对象的有限范围结构探针：

- `pb layout dump`
- 根头部 `pb q+0x...`
- 若干页样本 `pb page+0x...`
- `DXBC` / `DDS` marker 命中计数

同时 `ProbeThread` 仍保留 30 秒窗口，但只在 tick 1-5、10、15、20、25、30 进行替换尝试，减少重复日志。构建已通过。

## 2026-06-07 更新：跟随 pixel buffer 根对象指针

14:03 日志确认 `Texture+0x20` 根对象线性区域没有 `DXBC` 或 `DDS` marker，但根对象里存在多个指针字段：

```
+0x38, +0x88, +0xC8, +0xE0
```

这些字段指向其他 heap 区域，说明像素/页数据可能不在 `Texture+0x20` 后方的线性区域内。

本轮新增 `CustomJacketPointerProbe.cpp`，在 `pb layout dump` 后继续跟随候选字段：

- `+0x38`
- `+0x88`
- `+0x90`
- `+0xC8`
- `+0xD8`
- `+0xE0`

每个候选指针会输出：

- 指针地址与可读范围
- 目标块头部 qword
- `+0x48/+0x50` dword
- 目标块内 `DXBC` / `DDS` marker 命中计数

构建已通过。

## 2026-06-07 更新：14:08 指针探针结论

14:08 日志确认 `Texture+0x20` 根对象和候选指针字段目标块均未命中 `DXBC` / `DDS` marker：

```
pb marker DXBC count=0
pb marker DDS count=0
pb ptr field=0x38 DXBC count=0
pb ptr field=0x88 DXBC count=0
pb ptr field=0x90 DXBC count=0
pb ptr field=0xD8 DXBC count=0
pb ptr field=0xE0 DXBC count=0
```

本轮扩展 `CustomJacketPointerProbe.cpp`：

- 对 `Texture+0x20` 根对象和候选字段目标调用 vtable[0] 获取 RTTI 类型名。
- 对每个候选目标块前 `0x60` 字节内最多 5 个可读二级指针输出 `pb nested ...`。
- 每条 `pb ptr ...` 日志增加 `type=` 字段。

构建已通过。下一次运行日志应重点检查 `pb root type=`, `pb ptr ... type=`, `pb nested ...`。

## 2026-06-07 更新：14:12 播放崩溃与安全回退

14:12 日志中，点击播放自定义曲目后出现：

```
music play state idle(0) -> state5(5) trackId=0xAD900001
DLL_PROCESS_DETACH
```

没有进入 `state5 -> playing(1)`。崩溃发生在播放 state5 窗口。

同一轮日志确认页面样本里出现疑似非 4 字节对齐的 `DXBC` 片段：

```
pb page+0x40000 ... d50=0x42584400
pb page+0x80000 ... d50=0x42584400
```

因此旧的 4 字节对齐 marker 扫描会漏掉这类页。

本轮安全修正：

- 移除 `CustomJacketPointerProbe.cpp` 中对候选块 vtable[0] 的 RTTI 主动调用，避免对非普通对象调用虚函数。
- `DXBC` / `DDS` marker 扫描改为逐字节扫描。
- 页样本新增 `d51` / `d52` 输出，用于确认非对齐 marker 起点。
- `CloneAndReplacePixelBuffer()` 切为 probe-only：即使识别到 DXBC 页，也只记录日志，不 clone、不覆写、不 assign，避免播放时写入实验对象导致崩溃。

构建已通过。下一轮日志应验证点击播放是否恢复 `state5 -> playing(1)`，并检查 `dxbcPages` 与 `pb marker DXBC count` 是否开始命中。

## 2026-06-07 更新：14:17 probe-only 验证

14:17 日志确认 probe-only 后点击播放恢复正常：

```
music play state idle(0) -> state5(5) trackId=0xAD900001
music play state state5(5) -> playing(1)
```

因此 14:12 播放崩溃与写入/assign 实验对象或主动 RTTI 调用高度相关；probe-only 不会破坏播放。

同一轮日志还确认第一次 `pb layout dump` 发生时 readable 只有 `262144`，但后续 tick 中 readable 增长到 `9371648`。旧探针只 dump 首次，因此会错过后加载页面。

本轮新增：

- `pb layout dump` 按 readable 阈值重复输出：`0x40000`、`0x100000`、`0x400000`、`0x800000`。
- `bcn:` 日志增加 `dxbcMarkers=`，区分“页内存在 DXBC marker”和“DXBC 页头字段通过校验”。
- DXBC 页命中 probe-only 分支也会输出 layout dump，不执行替换。

构建已通过。下一轮日志应重点观察 readable 增大后的 `pb marker DXBC count`、`dxbcMarkers`、`dxbcPages`。

## 2026-06-07 更新：14:20 DXBC marker 已确认

14:20 日志确认 readable 增大后，`Texture+0x20` 线性区域和 `+0x88/+0xD8` 指针目标均可扫到 `DXBC` marker：

```
bcn: ... readable=1048576 ... dxbcMarkers=16 dxbcPages=0
pb marker DXBC count=31 hit0=0x20051 hit1=0x24051 hit2=0x40051 hit3=0x60051
pb ptr field=0x88 DXBC count=31 hit0=0x10051 hit1=0x14051 hit2=0x30051
```

这证明不是没有 DXBC 数据，而是当前 `ReadPageHeader()` 对 size/mip 字段的偏移假设错误，导致 `dxbcPages=0`。

本轮新增 `pb DXBC ctx ...` 输出，对前几个 DXBC 命中点 dump 周围关键字段：

- `hit - 0x50`
- `hit - 0x48`
- `hit - 0x40`
- `hit - 0x10`
- `hit + 0`
- `hit + 0x17`
- `hit + 0x18`
- `hit + 0x1B`

构建已通过。下一轮日志应依据 `pb DXBC ctx` 确认真实页头起点、payload size 和 mip count 字段。

## 2026-06-07 更新：14:23 日志分析与 14:28 重新部署

14:23 日志确认 probe-only 模式下播放外部曲目稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

之后暂停、恢复也正常，最后 `DLL_PROCESS_DETACH` 发生在用户退出游戏时，日志中没有播放点击导致崩溃的迹象。

同一份日志仍显示：

```
dxbcMarkers=29 dxbcPages=0
pb DXBC ctx hit=0x20051 ... d[18]=0x1054 d[1B]=0x700
```

但本次运行使用的是 14:22:34 的 `Ds2MusicPlayerExtend.asi`，而 `CustomJacketBcn.cpp` 中 `mipCount` 偏移修正在 14:25:33 才写入。因此这份日志不能验证新的 `marker + 0x1C` mip 读取逻辑。

14:28 已重新执行 `ds2_music_player_asi/build.ps1`，构建通过，并部署新的 ASI 到游戏目录：

```
scripts/Ds2MusicPlayerExtend.asi LastWriteTime=2026/6/7 14:28:11
```

下一轮运行应重点验证：

```
bcn: ... dxbcMarkers=N dxbcPages=M
bcn: probe-only mode; DXBC pages detected, replacement skipped
```

若 `dxbcPages > 0`，说明页头解析开始通过；仍应保持 probe-only，不立即启用覆写/assign。

## 2026-06-07 更新：14:33/14:34 新日志确认页头解析成功

14:33 新日志使用的是 14:28:11 部署的 ASI，已验证 `marker + 0x1C` mip 读取修正生效：

```
bcn: origPB=0x2351A020000 readable=655360 clone=589824 tiles=169x12 dxbcMarkers=5 dxbcPages=5
bcn: origPB=0x2351A020000 readable=1179648 clone=1179648 tiles=169x12 dxbcMarkers=17 dxbcPages=17
bcn: probe-only mode; DXBC pages detected, replacement skipped
```

这说明 `DXBC` marker 不只是能扫到，当前页头校验也能通过。典型上下文仍为：

```
pb DXBC ctx hit=0x20051 ... d[18]=0x4A0 d[1B]=0x700
pb DXBC ctx hit=0x40051 ... d[18]=0xA18 d[1B]=0x700
```

其中 `d[18]` 的低 16 位可作为 data size，`d[1B]` 显示的 `0x700` 来自 `+0x1C` 的 mip count byte 为 7。

同一轮日志确认 probe-only 下外部曲目播放稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

14:39 已新增 `DumpDXBCPageHeadersOnce()`，在 probe-only 命中时输出最多 12 个已解析页头：

```
bcn page header off=0x... marker=0x... dataStart=0x... dataSize=... mips=... pageEnd=0x...
```

构建通过，新的 `scripts/Ds2MusicPlayerExtend.asi` 时间戳为 2026/6/7 14:39:48。下一轮日志需要验证 `dataStart/pageEnd` 计算是否与实际页面边界一致；仍不启用覆写/assign。

## 2026-06-07 更新：14:41/14:42 页头日志确认

14:41 新日志使用的是 14:39:48 部署的 ASI，已输出 `bcn page header`：

```
bcn page header off=0x40000 marker=0x51 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x40a70
bcn page header off=0x60000 marker=0x51 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x60a70
bcn page header off=0x63000 marker=0x51 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x63a70
bcn page header off=0x70000 marker=0x51 dataStart=0x90 dataSize=3144 mips=7 pageEnd=0x70cd8
bcn page header off=0x80000 marker=0x51 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x80a70
```

确认事实：

- `DXBC` marker 起点稳定在页内 `0x51`，即页样本中的 `d51=0x43425844`。
- 7 mip 页的 payload 起点按 `0x70 + 7*4` 后 16 字节对齐得到 `0x90`。
- 当前能解析到的单页 `dataSize` 只有 2528 或 3144 字节，远小于旧 `OverwritePage()` 按 512x512 BC3 mip chain 计算的写入量。
- 因此旧的 512x512 连续 BC3 覆写假设不成立；继续按旧逻辑写会越过该页 payload 语义边界，即使写在 clone buffer 中也不能代表有效纹理数据。

同一轮日志继续确认 probe-only 下外部曲目播放稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

14:47 已更新下一版探针：

- `ProbePixelBuffer()` 的 `cloneSize` 计算改用 `dataStart + dataSize`，不再用旧的 `0x80 + dataSize` 近似。
- `bcn page header` 增加 `payloadDXBC=0x...`，用于确认 payload 内是否嵌套新的 `DXBC` 片段。
- `bcn page header` 增加 `mipOffs=...`，用于验证页内 mip offset table。

构建通过，新的 `scripts/Ds2MusicPlayerExtend.asi` 时间戳为 2026/6/7 14:47:34。下一轮日志需要验证 `payloadDXBC` 和 `mipOffs` 后，再决定是否做非常小范围的 clone-only 数据实验；仍不启用运行时 assign/覆写。

## 2026-06-07 更新：14:57/14:58 payloadDXBC 与 mipOffs 结论

14:57 新日志使用的是 14:47:34 部署的 ASI，首次输出 `payloadDXBC` 和 `mipOffs`：

```
bcn page header off=0x40000 marker=0x51 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x40a70 payloadDXBC=0xa6a mipOffs=0x3c00,0x4c00,0x8800,0xc400,0x17400,0x19000,0x9a800
bcn page header off=0x70000 marker=0x51 dataStart=0x90 dataSize=3144 mips=7 pageEnd=0x70cd8 payloadDXBC=0x0 mipOffs=0x3c00,0x4c00,0x5c00,0x6c00,0x12400,0x14000,0xc0c00
```

新结论：

- `payloadDXBC=0xa6a` 对应日志中的第二个 marker，如 `hit=0x40A6A`，说明某些页的 payload 内嵌套另一个 `DXBC` header。
- `mipOffs` 被读成 `0x3c00/0x4c00/...`，明显是字节序被错位 1 字节放大后的结果；真实偏移表应从 `marker + 0x20`，也就是页内 `0x71` 开始，而不是固定 `0x70`。
- tick 2 后 `readable` 可增长到 9MB，但 `pb marker DXBC count=0`，说明同一个 pixel buffer 地址的页内容会被流式替换/重排；仅在 tick 1 dump 一次页头不够。
- 后续 tick 又可回落到 327680 bytes 且 `dxbcPages=0`，这进一步证明 runtime pixel buffer 是动态虚拟纹理数据结构，不是稳定的连续图片缓冲。

播放仍稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

15:01 已更新下一版探针：

- `mipOffs` 改从 `marker + 0x20` 读取，并在日志增加 `table=0x...`。
- `dataStart` 也改用 `marker + 0x20 + mips*4` 后 16 字节对齐。
- `DumpDXBCPageHeadersOnce()` 改为按 readable 阈值重复输出，与 layout dump 一致，便于观察 0x40000/0x100000/0x400000/0x800000 阶段。
- `ResetPixelBufferDiagnostics()` 会同时清除页头 dump mask。

构建通过，新的 `scripts/Ds2MusicPlayerExtend.asi` 时间戳为 2026/6/7 15:01:30。下一轮日志重点看修正后的 `mipOffs` 是否变为 `0x3C/0x4C/...` 这类小偏移，以及 9MB 阶段是否还有可解析页头。

## 2026-06-07 更新：15:03 mipOffs 修正确认

15:03 新日志使用的是 15:01:30 部署的 ASI，确认 `mipOffs` 读取修正成功：

```
bcn page header off=0x40000 marker=0x51 table=0x71 dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x40a70 payloadDXBC=0xa6a mipOffs=0x3c,0x4c,0x88,0xc4,0x174,0x190,0x9a8
bcn page header off=0x70000 marker=0x51 table=0x71 dataStart=0x90 dataSize=3144 mips=7 pageEnd=0x70cd8 payloadDXBC=0x0 mipOffs=0x3c,0x4c,0x5c,0x6c,0x124,0x140,0xc0c
```

16MB 阶段也能解析出大量页头，说明上一轮 “9MB 阶段 marker 消失” 不是固定规律，而是运行时页内容随加载状态变化：

```
bcn: origPB=0x41DCA020000 readable=16777216 clone=10813440 tiles=169x31 dxbcMarkers=21 dxbcPages=16
bcn page header off=0x10a000 ... dataSize=3720 ... payloadDXBC=0xf12 mipOffs=0x3c,0x4c,0x10c,0x174,0x2c4,0x2e0,0xe50
bcn page header off=0x85c000 ... dataSize=7088 ... payloadDXBC=0x1c3a mipOffs=0x3c,0x4c,0x1cc,0x334,0x6cc,0x6e8,0x1b78
```

确认事实：

- 页内偏移表基址为 `marker + 0x20`，本轮为 `0x71`。
- `mipOffs[0]` 通常为 `0x3c`，这更像是相对 `marker` 的偏移；`marker + 0x3c = 0x8d`，与 offset table 末尾 `0x71 + 7*4 = 0x8d` 一致。
- 当前 `dataStart=0x90` 是按 16 字节对齐后的候选 payload 起点，不能再把它当作已完全确认的第一个 mip 起点。
- 高 readable 阶段的可解析页分布在 `0x10a000`、`0x113000`、`0x85c000`、`0x907000` 等位置，说明实际页表是离散虚拟纹理页，不适合做连续像素缓冲覆写。

播放仍稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

15:09 已新增下一版页头日志字段：

```
tableEnd=0x...
firstAbs=0x...
```

用于直接验证 `mipOffs[0]` 是否相对 `marker`，以及 first mip 是否从 `marker + mipOffs[0]` 开始。构建通过，新的 `scripts/Ds2MusicPlayerExtend.asi` 时间戳为 2026/6/7 15:09:05。

## 2026-06-07 更新：15:10 first mip 起点确认

15:10 新日志使用的是 15:09:05 部署的 ASI，确认 `tableEnd` 与 `firstAbs` 完全相等：

```
bcn page header off=0x80000 marker=0x51 table=0x71 tableEnd=0x8d firstAbs=0x8d dataStart=0x90 dataSize=2720 mips=7 pageEnd=0x80b30 payloadDXBC=0xb2a mipOffs=0x3c,0x4c,0xb0,0x118,0x20c,0x228,0xa68
bcn page header off=0x840000 marker=0x51 table=0x71 tableEnd=0x8d firstAbs=0x8d dataStart=0x90 dataSize=2528 mips=7 pageEnd=0x840a70 payloadDXBC=0xa6a mipOffs=0x3c,0x4c,0x88,0xc4,0x174,0x190,0x9a8
```

确认事实：

- `mipOffs[0]` 是相对 `marker` 的偏移。
- 第一个 mip 起点为 `marker + mipOffs[0]`，本轮为 `0x51 + 0x3c = 0x8d`。
- 16 字节对齐后的 `0x90` 只是旧候选值，不是实际 first mip 起点。
- `pageEnd` 后续应按 `firstAbs + dataSize` 计算，不应按 `alignedStart + dataSize`。
- 同一次运行中，tick 2-4 可解析 9MB 左右的 DXBC 页；tick 5 后 `readable=131072` 且 `dxbcPages=0`，再次说明 runtime pixel buffer 会随流式状态收缩/重排。

播放仍稳定：

```
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

15:14 已更新下一版代码：

- `ProbePixelBuffer()` 的 cloneSize/maxSeen 估算改用 `PayloadStart = marker + mipOffs[0]`。
- `FindPayloadDXBC()` 扫描起点改用 `PayloadStart`。
- 页头日志把旧 `dataStart` 改名为 `alignedStart`，避免误认为真实 first mip 起点。

构建通过，新的 `scripts/Ds2MusicPlayerExtend.asi` 时间戳为 2026/6/7 15:14:00。注意 `CustomJacketBcn.cpp` 已 291 行，后续新增代码前应优先拆分文件。

## 2026-06-07 更新：19:09 自有 PB 拷贝仍显示苹果的根因

19:09 最新日志确认当前已经不是 `altpb` 的 NO DATA 控制实验，而是重新
运行了 HotSpring pixelBuffer 的 VirtualAlloc 拷贝路径：

```
uiclone PB copy: srcPB=0x41990420000 newPB=0x1D068EC0000 size=655360 relocated=321 patchedPages=5
pbcmp begin hot=0x41990420000 hotSize=655360 noData=0x4198EF9EE00 noDataSize=4985344 clone=0x1D068EC0000 cloneSize=655360
uiclone OK: srcTexture=0x41990410000 newTexture=0x1D030713680
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
```

用户观察仍是苹果图。日志中的关键差异如下：

```
hot   +0x90 = 0x1D00171A430
hot   +0xE0 = 0x1D00171A450
noData+0x90 = 0x1D0016E9610
noData+0xE0 = 0x1D0016E9630
clone +0x88 = 0x1D068ED0000
clone +0xD8 = 0x1D068ED0000
clone +0x90 = 0x1D00171A430
clone +0xE0 = 0x1D00171A450
pbcmp clone refs-to-hot aligned=0 any=0
```

确认结论：

- clone 的 `+0x88/+0xD8` 已经重定位到自有 clone 内存。
- clone 内没有残留指向 HotSpring pixelBuffer 范围的引用。
- clone 的 `+0x90/+0xE0` 仍然是 HotSpring 原生 descriptor/resource
  blocks，而不是 clone 自有对象，也不是 NO DATA blocks。
- `pbres` dump 显示 clone 的 `f90/fE0` 内容与 HotSpring 完全一致。
- 因此当前画面仍为苹果图，是因为可见图像继续跟随 HotSpring GPU
  descriptor/resource 状态。

结合此前 NO DATA 控制实验，`TextureDX12` 根对象的 `+0x90/+0xE0`
已经被确认是当前显示结果的主导字段。CPU 侧 DXBC payload patch、
`+0x38` 外部块克隆和 `+0x88/+0xD8` 重定位都不足以创建新的可见 GPU 资源。

## 2026-06-07 更新：IDA 确认 descriptor/view 创建路径

IDA 精确 vtable 引用确认 `TextureDX12` vtable 为 `0x1433E5C98`：

- `sub_142112E30` 是 `TextureDX12` 构造函数，初始化时会清零
  `+0x90/+0xE0`。
- `sub_142113000` 是释放路径，会按两套 80 字节 stride 释放
  `+0x78/+0x80/+0x88/+0x90` 和 `+0xC8/+0xD0/+0xD8/+0xE0`。
- `sub_1420F2CF0(dstSlot, srcSlot)` 是 24 字节 descriptor handle 的安全复制函数。
- `sub_1420F34E0(pool)` 是 descriptor block allocator。
- `sub_142117000(dstTextureDX12, srcTextureDX12)` 是已确认的
  descriptor/view clone-or-alias 初始化路径。

`sub_142117000` 的关键行为：

1. 从源 `TextureDX12` 的 `+0x78` 或 `+0xC8` 槽复制 24 字节 descriptor handle。
2. 通过 `sub_1420F34E0(qword_14623FB38 + 8818080)` 给目标的
   `+0x90` 或 `+0xE0` 分配新 descriptor block。
3. 构造 view descriptor 参数。
4. 调用全局 GPU/D3D 接口 `xmmword_1463E0CB0` 的 vtable `+0x90`
   写入 descriptor block。
5. 设置目标 `TextureDX12+0x88 = 2`。

这说明当前 ASI 的 memcpy clone 缺少“分配 descriptor block + 调 GPU
接口创建 view”的步骤；仅重定位 CPU 指针不会产生新的可见 GPU 资源。

## 核心数据链

```
Track+0x50  →  slot {target, packed}
  target+0x20  →  loaded UITexture (0x100 bytes)
    UITexture+0x30  →  Texture (0x600 bytes)
      Texture+0x20  →  TextureDX12 / pixelBuffer runtime object
```

当前确认：链路本身正确，但 `Texture+0x20` 根对象内部的
`+0x90/+0xE0` descriptor/resource blocks 仍决定最终可见图像。

## 历史 DXBC 覆写实现 (CustomJacketPixelTest.cpp)

以下记录的是 15:14 前后的 DXBC payload 覆写实验路径。19:09 日志已经证明，
该路径即使 patch 了 CPU 副本 payload，仍会因为 `+0x90/+0xE0`
继承 HotSpring descriptor/resource blocks 而显示苹果图。

### 执行步骤

```
CloneAndReplacePixelBuffer():
  1. Clone pixel buffer (VirtualAlloc + memcpy 完整副本)
  2. 扫描副本中 "DXBC" FourCC 数据页 → 覆盖 mip chain 为彩色 BC3 测试图案
  3. Clone Texture (HeapAlloc 0x600, 复制 vtable+header, 修复 chain 指针)
     Texture+0x20 → 新 pixelBuffer
  4. Clone UITexture (HeapAlloc 0x100, 复制 + ResetObjectHeader)
     UITexture+0x30 → 新 Texture
  5. 构造新 target → SEH_AssignLoaded (vtable[3]) 安装到 Track+0x50
```

### 关键防御措施
- 所有读/写操作包在 `__try/__except` 中
- SEH 函数内不使用 C++ 对象（避免 C2712 编译错误）
- chain 指针全部修正指向新对象内部（避免 UAF）
- 日志记录每一步成功/失败

### 实际视觉效果
- 自有 pixelBuffer 副本稳定，播放稳定。
- CPU DXBC payload patch 已执行，但画面仍显示苹果图。
- 根因是 clone 的 `+0x90/+0xE0` 仍指向 HotSpring GPU descriptor/resource
  blocks。

## 测试步骤

1. 拷贝 `Ds2MusicPlayerExtend.asi` 到 `<GameRoot>\scripts\`
2. 启动游戏 → 音乐列表 → 选外部音源曲目
3. 观察专辑图是否变色
4. 查看日志：
   ```
   <GameRoot>\log.txt             (ASI 日志)
   <GameRoot>\ds2_dll_music_resource.log (runtime DLL 日志)
   ```

## 关键日志关注点

```
期待看到的日志序列:
custom jacket applied: target=...
tick=1 loaded=...
bcn: origPB=... size=... tiles=...
bcn: page[0x40000] mips=7 written=699008    ← DXBC 页面覆盖成功
bcn: page[0x60000] mips=6 written=...       ← 更多页面
bcn: overwrote N DXBC pages
bcn OK: ui=... tex=... pb=... pages=N        ← assign 成功
```

## 故障排查

| 现象 | 可能原因 | 检查点 |
|------|---------|--------|
| 崩溃 | Texture chain 指针指向错误位置 | `Texture+0x70→+0xE0→+0x150→+0x1C0→0` 链是否在分配块内 |
| 崩溃 | pixelBuffer 大小为 0 或不正确 | 是否扫描到 "DXBC" FourCC |
| 无日志 `bcn: overwrote 0` | 未找到 DXBC 页 | pixel buffer 格式是否变化 |
| 仍显示默认图 | assign 未生效 | 日志是否出现 "bcn OK" |

## 关键 IDA 地址

| 地址 | 描述 |
|------|------|
| `sub_140103CE0` | Texture 对象分配器 (参数: `&word_145E1F740`, 分配 0x70 字节) |
| `off_143119280` | Texture vtable (24 个槽位) |
| `sub_1424E5FC0` | GPU/MemoryMgr 绑定 (参数: pTexture, pGPUResource, pSizeInfo) |
| `sub_141D2F620` | 默认 Texture 构造 (512×512, format=31 RGBA8) |
| `sub_141D2F7B0` | 高斯模糊后处理 |
| `sub_1426D9F50→sub_1426D9CB0` | StreamingRef vtable[3] assign_loaded |

## Decima Pixel Buffer 格式 (DXBC 页)

```
Page 布局 (每页 64KB 对齐):
+0x00: heap vtable ptr
+0x08: type flag (0x0C = pixel data)
+0x10: size hint
+0x18: format flag (0x32)
+0x30: content hash (16 bytes)
+0x40: pixel payload size
+0x48: "DXBC" FourCC
+0x4C: variant byte
+0x50: format-specific header
+0x68: pixel data size
+0x6C: mip count
+0x70: mip offset table (mips × 4 bytes)
+0x70+mips*4: pixel data start (16-byte aligned)
```

## 2026-06-07 19:57 更新：GPU resource object 已确认仍继承 HotSpring

19:09 日志中 `pbres wrapper` 已经证明 clone 与 HotSpring 共用真实
GPU resource/ref object：

```text
hot.f88+0x8    = 0x1D04DDE40F0
noData.f88+0x8 = 0x1D03ADC02D0
clone.f88+0x8  = 0x1D04DDE40F0

hot.f88+0x30    = 0x41990420018
noData.f88+0x30 = 0x4198EF9EE18
clone.f88+0x30  = 0x1D068EC0018
```

clone 的 wrapper `+0x30` CPU data 指针已经指向自有 patched buffer，
但 `wrapper+0x8` 仍是 HotSpring 的 resource object。结合 IDA 中
`TextureDX12_bind_resource_handle_create_views` (`0x142116B40`) 的行为，
可见图像仍为苹果图的根因不是单纯 descriptor block 没分配，而是
descriptor/view 继续基于 HotSpring 的 GPU resource object 创建。

IDA 数据库已重命名并注释这些已确认函数：

| 地址 | 名称 | 职责 |
|------|------|------|
| `0x142116B40` | `TextureDX12_bind_resource_handle_create_views` | 绑定 engine resource handle 并创建 descriptor |
| `0x142117000` | `TextureDX12_clone_resource_handle_create_view` | 复制源 handle 并新建 view，但仍引用源 GPU resource |
| `0x142118A40` | `TextureDX12_create_srv_uav_descriptors` | 调 GPU/D3D 接口创建 view descriptor |
| `0x140D18D20` | `D3DResourceManager_create_resource_wrapper` | 高层 D3D resource wrapper 创建入口 |
| `0x140D19170` | `D3DResourceManager_create_placed_resource` | 创建 D3D12 heap/placed resource |
| `0x142113810` | `TextureDX12_upload_texture_payload` | 通过 Map/WriteToSubresource 或 upload queue 写入 GPU resource |

本轮代码只新增诊断：`CustomJacketPixelBufferGpuResource.cpp` 会打印
`pbres link f88 ... cloneResEqHot=...` 和 resource object vtable 槽位。
`ds2_music_player_asi\build.ps1` 已在 2026-06-07 19:57 成功执行，输出
`BUILD_OK`。可以启动游戏获取这组新日志。

## 2026-06-07 21:35-21:41 更新：txupload 历史匹配已验证

21:35 后的运行使用禁用 `txbind` 的稳定 ASI，外部曲目继续正常
`state5 -> playing -> paused`，没有崩溃。

新增 `TextureUploadHistory` 后，日志已把 upload 调用与原生 jacket
`TextureDX12` 地址对齐：

```text
txupload match noData call=905 tex=0x5B94B39EE00 ... callerRva=0x24E6BE7
txupload match hot    call=4027 tex=0x5B94C820000 ... callerRva=0x24E6BE7
txupload matches hot=1 noData=1 clone=0 seen=4798 capacity=4096
```

确认结论：

- NO DATA 和 HotSpring 原生 jacket 都走过
  `TextureDX12_upload_texture_payload`。
- 当前 clone 没有 upload 记录，仍只是 CPU 对象副本。
- clone 继续显示 HotSpring 的原因与此前一致：
  `clone.resource == hot.resource`，且 `clone` 的 descriptor blocks 也继承
  HotSpring。

21:39 日志补齐 HotSpring reader qword：

```text
txupload match hot call=4027 ... rq8=0x0 rq10=0x2 rq18=0x4F04C8001E0
rq20=0x7FF710C81230 rq28=0x28989FFAD0 rq30=0x4F000000000
rq38=0x67AA4322000016
```

因为 4096 环形缓冲不足以保留 6066 次 upload 中较早的 NO DATA 记录，
当前已把 `TextureUploadHistory` 容量扩大到 `16384` 并构建通过。
最新部署 ASI 时间戳为 `2026/6/7 21:41:35`。

## 2026-06-07 21:42 更新：NO DATA 与 HotSpring reader qword 已同时保留

21:42 日志确认扩容后的 `TextureUploadHistory` 可同时保留两张原生
jacket 的 upload 记录：

```text
txupload match noData call=905 tex=0x4E98CF4EE00 ... callerRva=0x24E6BE7
txupload match hot    call=4027 tex=0x4E98E420000 ... callerRva=0x24E6BE7
txupload matches hot=1 noData=1 clone=0 seen=4840 capacity=16384
```

reader qword 对比：

```text
noData: rq8=0x0 rq10=0x4E900000002 rq18=0x4E9948001E0
        rq20=0x7FF710C81230 rq28=0x900E3FFB70 rq30=0x0
        rq38=0x59E12184000013

hot:    rq8=0x0 rq10=0x2 rq18=0x4E9948001E0
        rq20=0x7FF710C81230 rq28=0x900EFFF780 rq30=0x4E900000000
        rq38=0x67AA4322000016
```

确认结论：二者 `readerVt` 和 `callerRva` 相同，`rq18/rq20` 相同，
`rq28` 指回 reader 自身；差异集中在 `rq10/rq30/rq38`。clone 仍然没有
upload 记录。

## 2026-06-07 21:48 更新：reader 内存区域已记录

21:48 日志确认 NO DATA 和 HotSpring 的 reader 都位于 4KB
`MEM_PRIVATE` / `PAGE_READWRITE` committed 区域：

```text
noData reader=0x1846DFF880 readerBase=0x1846DFF000
readerRegion=4096 readerProtect=0x4 readerType=0x20000 readerState=0x1000

hot reader=0x1847DFF690 readerBase=0x1847DFF000
readerRegion=4096 readerProtect=0x4 readerType=0x20000 readerState=0x1000
```

本轮只确认 reader 所在 VAD 属性，尚不能仅凭该信息判断是否为线程栈页。
当前代码已增加 `GetCurrentThreadStackLimits` 记录，下一轮日志将直接输出
reader 是否落在线程栈范围内。

## 2026-06-07 22:00 更新：txupload caller 与 reader 契约静态确认

围绕已知 `callerRva=0x24E6BE7` 的反汇编确认，原生 jacket upload 来自
`0x1424E6BE3` 的 `TextureDX12` vtable slot 2 调用，`0x24E6BE7` 是该调用后
的返回地址：

```text
1424E6BCF mov rcx, [rdi]      ; TextureDX12
1424E6BD2 mov rdx, rsi        ; reader
1424E6BE0 mov r10, [rcx]
1424E6BE3 call qword ptr [r10+10h]
```

同一函数入口 `0x1424E6100` 确认 reader 指针来自第二参数 `rdx`，并先通过
reader vtable `+0x18` 读取 0x20 字节 header。caller 在 upload 前后调用
reader vtable `+0x30` 取当前位置，用差值记录本次读取量。

`TextureDX12_upload_texture_payload` (`0x142113810`) 内部确认：

- reader 会被 `0x1400B0A30` 连续读取 32-bit 字段；该 helper 调 reader
  vtable `+0x18` 并按 `reader+0x0C` 处理字节序。
- 上传阶段依赖 `TextureDX12+0x80/+0x88` 已存在 resource handle/wrapper。
- 函数会调用 resource object vtable `+0x50` 取 desc、reader vtable `+0x18`
  读取 payload、reader vtable `+0x20` 跳过/seek、resource object vtable
  `+0x60` 写入 subresource。
- 另一条路径调用全局 GPU 接口 `xmmword_1463E0CB0` vtable `+0x130` 调度上传。

确认结论：`TextureDX12_upload_texture_payload` 不是 resource 创建入口，它只把
上层 reader 数据写进已绑定 GPU resource。clone 当前没有自有 `wrapper+0x8`
resource object，因此直接硬调 upload 或伪造 reader 不能替代
resource wrapper 创建、bind 与 view 创建阶段。

## 2026-06-07 22:08 更新：native wrapper -> bind 顺序已确认

围绕已知 native 范例 `0x1420C2BC0` 的反汇编确认，它展示了完整的
resource wrapper 创建与 `TextureDX12` 绑定顺序：

```text
1420C2C5A call 140D18D20    ; D3DResourceManager_create_resource_wrapper
1420C2C6F call 140D19F10    ; register/track created wrapper
1420C2CAE call 1420F5FA0    ; 给 wrapper+0x8 resource 设置 debug name
1420C2CCE call 142112E30    ; TextureDX12_ctor_init_root
1420C2CE2 call 142116B40    ; TextureDX12_bind_resource_handle_create_views
```

`TextureDX12_bind_resource_handle_create_views` 的入参不是裸 resource 指针，
而是合法的 24-byte engine resource handle slot。`sub_1420F2CF0` 已确认会
复制该 slot，并对 `slot+0x08` 的 resource/ref object 做 addref/release。

22:20 进一步静态确认本 native 范例的 handle slot 为
`[0x0304, 0, wrapper]`。`TextureDX12_bind_resource_handle_create_views`
入口在 `slot+0x10` 非零时读取 `wrapper+0x08` 作为真实
resource/ref object；因此 wrapper-backed slot 的真实 resource 不直接放在
`slot+0x08`。同一函数里的 `a2/a3` 是 resource width/height，resource
描述结构包含 `dimension=3`、`format=0x1C`、`flags=0x10001`、`usage=0x21`。
`0x1420F5FA0` 已重命名为
`D3DResource_set_debug_name_from_decima_string`；它只是调用 resource vtable
`+0x30` 设置 debug name，不构造 handle slot。

当前可靠边界：自有专辑图必须先创建合法 engine resource wrapper/handle slot，
再交给 bind/create-view 路径；`TextureDX12_upload_texture_payload` 只能在
已有 resource 上写数据，不能替代 resource 创建。

## 2026-06-07 22:04-22:05 更新：readerOnStack 已验证

22:04/22:05 运行使用 `2026/6/7 21:50:57` 部署的 ASI。外部曲目稳定完成
播放、暂停、恢复、再次暂停，没有运行中崩溃：

```text
idle -> state5 -> playing -> paused -> playing -> paused
```

本轮补齐 `GetCurrentThreadStackLimits` 字段，确认 NO DATA 和 HotSpring 的
upload reader 都落在线程栈范围内：

```text
noData reader=0xE3241FF520 stackLow=0xE323E00000
stackHigh=0xE324200000 readerOnStack=1

hot reader=0xE324DFF450 stackLow=0xE324A00000
stackHigh=0xE324E00000 readerOnStack=1
```

同一轮继续确认：

```text
txupload matches hot=1 noData=1 clone=0 seen=4858 capacity=16384
clone.resource == hot.resource
clone.cpu != hot.cpu
```

确认结论：原生 upload reader 是调用栈上的临时 reader，不是可长期保存或
直接复用的 heap 对象。clone 仍未走 upload，且真实 GPU resource 仍继承
HotSpring。

## 2026-06-07 22:20 更新：handle slot 诊断已加入

本轮只新增运行时诊断，不改变专辑图替换逻辑，不重新启用已确认不稳定的
`txbind` 入口 hook。

`CustomJacketPixelBufferGpuResource.cpp` 现在会在三方 pixelBuffer 对比时额外
输出：

```text
pbres handle slot78 ...
pbres handle slotC8 ...
```

这两行记录 HotSpring、NO DATA、clone 的 `TextureDX12+0x78` 与
`TextureDX12+0xC8` 两个 24-byte handle slot 三个 qword，并打印 clone
与 HotSpring / NO DATA 的等同性字段。该诊断用于下一次运行时确认 clone
当前继承的 handle slot 具体落在哪些 qword 上。

`ds2_music_player_asi\build.ps1` 已在 2026-06-07 22:20 执行成功，输出
`BUILD_OK`。下一次启动游戏后重点查看 `pbres handle slot78/slotC8` 日志。

## 2026-06-07 22:54 更新：GPU resource 诊断文件拆分与构建确认

本轮只做工程整理和未接入的 helper 准备，不改变当前专辑图替换运行路径。

- `CustomJacketPixelBufferGpuResource.cpp` 已拆分出
  `CustomJacketPixelBufferHandleSlots.cpp`，用于保持单个代码文件低于 300 行。
- `pbres handle slot78/slotC8` 诊断行为保持不变。
- 新增 `CustomJacketTextureDx12Bind.cpp`，封装了基于
  `[0x0304, 0, wrapper]` handle slot 调用
  `TextureDX12_bind_resource_handle_create_views` 的 helper；当前默认未调用，
  尚无运行时验证结论。
- 该 helper 已加入 bind 地址模块范围校验；调用失败时会恢复调用前的
  TextureDX12 resource/view 字段。
- `ds2_music_player_asi\build.ps1` 已在 2026-06-07 22:56 执行成功，输出
  `BUILD_OK`；部署 ASI 时间戳为 2026-06-07 22:56:06。

## 2026-06-07 22:58 更新：handle slot 运行诊断已确认

22:58 运行使用 22:56 部署的 ASI。`pbres handle slot78/slotC8` 诊断已出现：

```text
pbres handle slot78 hot=[0x304,0x0,0x4FA93830000]
  noData=[0x304,0x0,0x4FA92389600]
  clone=[0x304,0x0,0x26BC7630000]

pbres handle slotC8 hot=[0x304,0x0,0x4FA93830000]
  noData=[0x304,0x0,0x4FA92389600]
  clone=[0x304,0x0,0x26BC7630000]
```

确认结论：

- HotSpring、NO DATA、clone 的 `TextureDX12+0x78` 与 `+0xC8` 都是
  wrapper-backed slot，形态均为 `[0x304, 0, wrapper]`。
- clone 的 handle slot `q10` 已经指向 clone 自有 wrapper，不再是
  HotSpring wrapper。
- 但 clone wrapper 的 `+0x8` 真实 resource 仍等于 HotSpring：

```text
hot.resource=0x26B8F8148B0
noData.resource=0x26B7C7B96B0
clone.resource=0x26B8F8148B0
cloneResEqHot=1
cloneCpuEqHot=0
```

- clone 的 descriptor blocks 仍等于 HotSpring：
  `clone.f90=hot.f90=0x266E3AAA430`、
  `clone.fE0=hot.fE0=0x266E3AAA450`。
- upload 历史仍为 `hot=1 noData=1 clone=0`，clone 没有走 upload。
- 外部曲目仍正常 `idle -> state5 -> playing -> paused`，没有运行中崩溃。

因此当前失败点已进一步缩小：不是 24-byte handle slot 没重定位，而是
clone wrapper 内部仍 memcpy 继承 HotSpring 的 engine resource object。
下一步仍应围绕创建自有 engine resource wrapper/resource object，而不是
继续处理 CPU payload 或 slot 指针。

## 2026-06-07 23:04 更新：手动 native bind 到 NO DATA wrapper 已成功返回

23:03/23:04 运行使用 23:02 部署的 ASI。本轮接入的
`TryBindTextureDx12ToSourceWrapper(clonePB, noDataPB)` 已执行，并且
`TextureDX12_bind_resource_handle_create_views` 调用成功返回，没有触发
SEH 失败路径：

```text
txdx12bind begin label=DefaultConstructionHoloImageTexture
  tex=0x1F4D7FA0000 source=0x34A9AF5EE00
  wrapper=0x34A9AF896C0 resource=0x1F4BAB180D0
  slot=[0x304,0x0,0x34A9AF896C0]

txdx12bind pre     s88=0x1F4D7FB0000 d90=0x1F48075B430
                   sD8=0x1F4D7FB0000 dE0=0x1F48075B450
txdx12bind cleared s88=0x0 d90=0x0 sD8=0x0 dE0=0x0
txdx12bind post    s88=0x34A9AF896C0 d90=0x1F48077C3D0
                   sD8=0x0 dE0=0x0
```

确认结论：

- 在 clone PB 构造完成后的探针线程里，直接调用
  `TextureDX12_bind_resource_handle_create_views` 是可返回的；这不同于
  对 `0x142116B40` 做入口 hook，后者仍是已确认不稳定路径。
- bind 后 clone 的主资源槽 `+0x88` 已从 clone wrapper 改为 NO DATA wrapper，
  `+0x90` 分配了新的 descriptor block。
- bind 没有填充第二套 `+0xD8/+0xE0`，post 状态为 0。
- 本轮没有 clone upload 记录变化：bind 实验只创建/绑定 view，不执行 upload。
- 外部曲目仍正常 `idle -> state5 -> playing -> paused`，没有运行中崩溃。

该实验验证了：手动调用 native bind/create-view 的最小调用边界可用，但它只能
把 clone 绑定到一个已存在 wrapper/resource。最终自定义 PNG 仍需要先创建自有
engine resource object；否则绑定到 NO DATA/HotSpring wrapper 只会显示已有图。

## 2026-06-07 23:14 更新：clone wrapper + NO DATA resource bind 已确认

23:10/23:14 运行使用 23:07 部署的 ASI。本轮实验不再直接绑定到 NO DATA
wrapper，而是保留 clone 自有 wrapper，将 clone wrapper `+0x08` 临时改成
NO DATA 的 resource object，再以 clone wrapper 作为 `[0x304,0,wrapper]`
调用 `TextureDX12_bind_resource_handle_create_views`。

关键日志：

```text
txdx12bind clonewrap begin
  tex=0x234D95F0000
  cloneWrapper=0x234D9600000
  oldResource=0x234CC3198B0
  sourceWrapper=0x33950787F80
  sourceResource=0x234BB0B52A0
  slot=[0x304,0x0,0x234D9600000]

txdx12bind clonewrap pre
  s88=0x234D9600000 d90=0x23480749430
  sD8=0x234D9600000 dE0=0x23480749450

txdx12bind clonewrap post
  s88=0x234D9600000 d90=0x2348076BF10
  sD8=0x0 dE0=0x0

txdx12bind clonewrap result
  wrapper=0x234D9600000
  resource=0x234BB0B52A0
  resourceEqSource=1
  wrapperEqClone=1
```

确认结论：

- bind 后 `TextureDX12+0x88` 保持 clone 自有 wrapper。
- clone wrapper `+0x08` 已指向 NO DATA resource object。
- `TextureDX12+0x90` 分配了新的 descriptor block。
- 第二套 `+0xD8/+0xE0` 仍未填充。
- 本轮仍没有 clone upload 记录，实验只验证 resource/view 绑定。
- 外部曲目稳定完成 `idle -> state5 -> playing -> paused -> playing -> paused`。

该实验确认：只要 clone wrapper 内的真实 resource object 正确，native
bind/create-view 可以基于 clone 自有 wrapper 工作。当前最终缺口仍是创建
自有 engine resource object，而不是 wrapper-backed slot 或 bind 调用边界。

## 2026-06-07 23:22 更新：原始 committed resource 创建参数失败

23:22 运行使用 23:19 部署的 ASI。本轮在 clone 构造后尝试基于 NO DATA
resource 的 `GetDesc()` / `GetHeapProperties()` 创建自有 D3D12 committed
resource。创建阶段失败，未进入后续 bind：

```text
txdx12own create desc dim=3 width=512 height=320 mips=1
  format=99 flags=0x0 heapType=1 heapFlags=0x44 hr=0x80070057
```

同一轮仍确认 clone 创建链稳定，且外部曲目正常播放到暂停：

```text
uiclone OK: ... newTexture=0x1F8FC303AF0
music play state idle(0) -> state5(5) trackId=0xAD900001 external=1
music play state state5(5) -> playing(1) trackId=0xAD900001 external=1
music play state playing(1) -> paused(2) trackId=0xAD900001 external=1
```

确认结论：

- 23:22 实验的失败点发生在 `CreateCommittedResource` 参数校验阶段，
  返回 `0x80070057`。
- 本轮没有进入 `txdx12own begin/pre/post`，因此未验证自有 resource
  绑定到 clone wrapper 后的行为。
- 该失败没有破坏自建 target / cloned `UITexture` / cloned `Texture`
  链路，也没有造成运行中崩溃。

23:31 已构建并部署下一版 ASI，保留同一实验边界，同时增加完整
`D3D12_RESOURCE_DESC` 与多组创建参数日志。构建输出为 `BUILD_OK`，
部署文件时间戳为 `2026/6/7 23:31:31`。

## 2026-06-08 20:10 更新：自有 D3D12 resource 创建与 bind 已成功

20:10 运行使用 23:31 部署的 ASI。原始 heap flags 仍失败，但去掉
`heapFlags=0x44` 后 committed resource 创建成功：

```text
txdx12own create raw dim=3 align=65536 width=512 height=320
  depthArray=1 mips=1 format=99 samples=1/0 layout=0
  flags=0x0 heapType=1 heapFlags=0x44 state=0x0 hr=0x80070057
txdx12own create raw-default-flags dim=3 align=65536 width=512 height=320
  depthArray=1 mips=1 format=99 samples=1/0 layout=0
  flags=0x0 heapType=1 heapFlags=0x0 state=0x0 hr=0x0
```

随后 clone wrapper 被绑定到该自有 resource，并成功创建新的 descriptor：

```text
txdx12own begin ... cloneWrapper=0x16BCA830000
  oldResource=0x1708E11CB90 ownResource=0x170F465CB40
  sourceResource=0x16BB0DF9060
txdx12own post tex=0x16BCA820000
  s88=0x16BCA830000 d90=0x16B80776910 sD8=0x0 dE0=0x0
txdx12own result wrapper=0x16BCA830000 resource=0x170F465CB40
  resourceEqOwn=1 wrapperEqClone=1
```

同一轮外部曲目稳定完成：

```text
idle -> state5 -> playing -> paused
```

确认结论：

- `heapFlags=0x44` 不能直接用于本次 committed resource 创建；
  `heapFlags=0` 可创建与 NO DATA 描述一致的自有 D3D12 texture resource。
- clone wrapper `+0x08` 已可指向自有 resource，且 native bind/create-view
  成功使用该 wrapper。
- bind 后 `TextureDX12+0x90` 变为新 descriptor block，
  `+0xD8/+0xE0` 仍为 0。
- 本轮尚未上传自定义像素到自有 resource，因此还不能确认可见图是否改变。

## 2026-06-08 20:29 更新：D3D12 upload/copy 成功但视觉仍无图

20:29 运行日志确认，自有 D3D12 resource 创建成功后，当前 helper 继续完成
upload buffer 准备、D3D12 copy、等待提交完成，以及 clone wrapper bind：

```text
txdx12upload prepared-bc7 width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12upload copy width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12own result wrapper=0x25DDB090000 resource=0x25E08656E70
  resourceEqOwn=1 wrapperEqClone=1
```

同一轮外部曲目仍稳定：

```text
idle -> state5 -> playing -> paused
```

用户随后反馈看不到专辑图。

确认结论：

- 当前 resource 创建、upload buffer、`CopyTextureRegion`、fence wait、
  clone wrapper bind/create-view 都已成功返回。
- 20:29 版虽然让 clone wrapper 指向了自有 resource，但游戏内仍未显示
  自定义专辑图。

## 2026-06-08 20:42 更新：合法 BC7 测试图已经显示

20:42 运行使用 20:38 部署的 ASI。该版本把测试图数据从任意 16-byte block
改为合法 BC7 mode 6 solid-color block。日志确认 resource 创建、上传、copy、
bind 均成功：

```text
txdx12own create raw-default-flags dim=3 align=65536 width=512 height=320
  depthArray=1 mips=1 format=99 samples=1/0 layout=0
  flags=0x0 heapType=1 heapFlags=0x0 state=0x0 hr=0x0
txdx12own begin label=DefaultConstructionHoloImageTexture
  tex=0x2A8B2470000 cloneWrapper=0x2A8B2480000
  oldResource=0x2AD8FEDB0F0 ownResource=0x2ADFD172770
  sourceResource=0x2A8B0F62B50
txdx12upload prepared-bc7 width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12upload copy width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12own result wrapper=0x2A8B2480000 resource=0x2ADFD172770
  resourceEqOwn=1 wrapperEqClone=1
```

同一轮播放稳定：

```text
idle -> state5 -> playing -> paused
```

用户视觉确认看到了测试图像。

确认结论：

- 外部曲目专辑图现在已经可以显示我们上传到自有 D3D12 resource 的测试图。
- 当前最小可行链路为：
  自建 target / cloned `UITexture` / cloned `Texture` /
  clone wrapper `+0x08` 指向自有 D3D12 resource /
  D3D12 upload buffer + `CopyTextureRegion` /
  `TextureDX12_bind_resource_handle_create_views` 创建 view。
- `format=99` 的源图需要合法 BC7 block。20:29 版任意 block 不显示；
  20:42 版合法 BC7 mode 6 block 已显示。

## Git 状态

当前分支：`master`
最后提交：`816bb98 update`
待提交文件以 `git status --short` 为准；当前文档只记录已确认逆向结论。
