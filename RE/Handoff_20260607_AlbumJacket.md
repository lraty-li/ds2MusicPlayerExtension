# 专辑图功能交接文档

## 日期

2026-06-07

## 目标回顾

让 DS2 外部音源自定义曲目显示**不同颜色的测试专辑图**（最终目标：加载 PNG 自定义图片）。

## 当前状态

**构建通过。** 当前部署版处于 **probe-only 安全探针模式**：只探测 `Texture+0x20` 的 pixel buffer、输出 DXBC 命中与结构日志，不执行 clone、覆写或 assign。clone pixel buffer + 覆盖 DXBC 页 + 构造新 Texture 的代码路径保留，但在确认 DXBC 页解析稳定前不启用。

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

## 核心数据链

```
Track+0x50  →  slot {target, packed}
  target+0x20  →  loaded UITexture (0x100 bytes)
    UITexture+0x30  →  Texture (0x600 bytes)
      Texture+0x20  →  pixelBuffer (VirtualAlloc, 640KB~9MB)
```

每个环节都操作的是**我们 clone 的独立对象**，不碰游戏原始对象。

## 新实现 (CustomJacketPixelTest.cpp)

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

### 预期视觉效果
- 成功：自定义曲目显示**彩色条纹专辑图**（mip 0=红, mip 1=绿, ...）
- 失败：日志中会看到 `bcn:` 开头的错误信息

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

## 下一步方向

**若彩色测试图案成功显示**，下一步替换为真正的 PNG 自定义图片：

1. 添加 `stb_image.h` 加载 PNG → RGB/RGBA 解码
2. 添加 BC3 编码器 (`bc7enc` 库或手写 BC3 encoder)
3. 构建完整 mip chain (`DirectXTex` 的 `GenerateMipMaps`)
4. 替换 `FillBC3Block` 为真实像素数据
5. 读取 PNG 路径（可硬编码为 `scripts\ds2_music_player_jacket.png`）

**若测试图案失败 (崩溃或无效显示)**，退回安全版本：
- Git `checkout` 旧版 `CustomJacketPixelTest.cpp`（仅 clone UITexture + 复用 Texture）
- 重新分析 pixel buffer 格式差异

## Git 状态

当前分支：`master`
最后提交：`816bb98 update`
待提交文件：
- `CustomJacketPixelTest.cpp` (重写)
- `CustomJacketPixelTest.h` (未改动)
- `RE/CustomJacketImplementationPlan.md` 需更新
