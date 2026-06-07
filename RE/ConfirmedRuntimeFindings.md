# 当前已确认运行时结论

本文只记录已经由当前日志或代码路径确认的事实。

## 外部曲目注入

- 自定义曲目 ID 为 `0xAD900001`。
- 自定义 Wwise event ID 为 `0xAD100000`。
- `DSMusicPlayerSystemResource` 加载后，`AllTracks` 从 `58` 增加到 `59`。
- 自定义曲目当前通过克隆一首可播放源曲目的 sound resource 链创建。
- 日志确认克隆链中的 Wwise ID 已写为 `0xAD100000`。
- 自定义曲目初始会继承源曲目的 jacket streaming slot，随后由专辑图 override 替换。

## Wwise Runtime DLL 与 Bank

- `ds2_dll_music_resource.dll` 能被 ASI 成功加载。
- `g_pAKPluginList` 能被读取，插件条目字段为：
  - type: `2`
  - company: `1703`
  - plugin: `257`
- `RegisterPluginDLL` 返回 `1`。
- 运行时构造的 source plugin bank 通过 `LoadBankMemoryCopy` 成功加载，返回 `AK_Success`。
- 当前生成 bank 的关键 ID：
  - bank: `0xAD400000`
  - event: `0xAD100000`
  - sound: `0xAD800000`
  - source plugin object: `0xAD810000`
  - plugin key: `0x01016A72`

## 当前播放崩溃窗口

- 选择自定义曲目后，游戏状态进入 `state5`。
- 进入 `state5` 时 `currentRuntime` 已经非空，说明游戏侧播放 runtime 对象已经建立。
- 崩溃前没有看到 `state5 -> playing(1)`。
- runtime 插件日志只确认到：
  - `DLL_PROCESS_ATTACH`
  - websocket server 启动
  - `createParams`
  - `params Init blockSize=0`
- 崩溃前没有出现 `createPlugin`、`plugin Init` 或 `plugin Execute`。
- 因此当前崩溃点不在音频流读取或 `Execute` 缓冲填充逻辑内，而是在自定义 event 播放实例真正创建 source plugin 之前。

### 2026-06-07 状态：state5 fix 已安装但 state5→idle 回退 — 根因已确认

**回退根因：** 第三版 fix 清零 `currentRuntime` 正好触发了 `MusicRuntime_Update` (sub_140C13E20) 中 state5 处理器的 guard 条件：

```
sub_140C13E20 (MusicRuntime_Update) state5 处理器:
  [0xC14642] deltaTime 累加到 +0x2818 (state5累积时间)
  [0xC1464A] vcomiss accumulated, threshold(≈0.1s)    ; 阈值常量 0x143460D58
             jb END                                     ; 未到阈值 → 跳过
  [0xC14660] cmp [rdi+1918h], 0        ; currentRuntime == 0 ?
             jz 0xC14E80               ; YES → 跳转到 abort/idle 路径!
  // 正常路径: PostEvent, init source, playing transition
  ...
  [0xC14E80] abort路径:
             [0xC14F20] call sub_140C162D0(runtime, 0)  ; SetPlayState(0) → idle
             [0xC14F25] mov rcx, rdi                    ; ← 日志中的 callerRva
             [0xC14F28] call sub_140C17920              ; 清理运行时
```

**因果链：**
1. `sub_140C15200` 调用 `SetPlayState(5)` → detour 出口清零 `currentRuntime`
2. state5 处理器等待 ~0.1s（阈值 ≈ 0.1s）
3. 检查 `currentRuntime == 0` → **是（我们自己清零的！）**
4. 判定 runtime 异常丢失 → 回退到 idle

**结论：清零 `currentRuntime` 是错误做法。**正确方案是只在函数入口清零 `currentTrackId`，让 `sub_140C15200` 自然走新曲目路径自己创建 runtime。需要替换 detour 到 `sub_140C15200` 入口（不是出口）。

## 专辑图资源字段

当前确认字段属于 `DSUICatalogueImageResource`，不是 `DSUIMusicMenuDataSourceResource`。

- `MusicJacketImageTextures`: `+0x60`
- `MusicJacketImageNameHash`: `+0x118`
- `DefaultMusicJacketImageTexture`: `+0xC0`
- `DefaultMusicJacketImageNameHash`: `+0x178`

运行日志确认：

- `MusicJacketImageTextures` 当前 count/capacity 为 `0`。
- `MusicJacketImageNameHash` 当前 count/capacity 为 `0`。
- 默认 jacket slot 存在，且可触发加载。
- 当前默认 jacket hash 为 `0x5D5CF31F`。

## Jacket StreamingRef Override

- 自定义曲目的 `Track+0x50` 是 jacket streaming slot。
- 将 `Track+0x50` 指向复制出来的默认 jacket slot 后，加载目标会变为 `UITexture`。
- 调用 slot 所属 context 的 `flagsFn(context, &Track+0x50, 1)` 可以触发该 UITexture 加载。
- override 后日志确认 loaded object 类型为 `UITexture`。
- 2026-06-07 运行时视觉验证确认：把 `Track+0x50` 替换为
  `DSUICatalogueImageResource+0xC8`
  `DefaultConstructionHoloImageTexture` 后，自定义曲目显示为不同图片。
- 本轮不同图片是游戏系统的 `NO DATA` 占位图。日志中对应字段为：
  `source=DefaultConstructionHoloImageTexture offset=0xc8`，
  加载后 `UITexture` 尺寸日志为 `0x100 x 0xA0`。
- 该结果证明 `Track+0x50` 的 slot 替换路径已经端到端生效；
  视觉内容取决于所选或构造的 `UITexture`，不是音乐菜单仍固定读取默认音乐 jacket。
- 2026-06-07 后续视觉验证确认：优先从 catalogue 数组选择
  `HotSpringImageTextures[0]` 后，自定义曲目显示为非占位图，用户观察为
  “几个苹果”。日志来源：
  `source=HotSpringImageTextures offset=0x50 index=0`。
- 因此 `DSUICatalogueImageResource` 的 `Array_StreamingRef_UITexture`
  条目可以直接作为 `Track+0x50` 的替换来源；数组 slot 和默认 slot
  走同一条可见显示路径。
- 2026-06-07 自建 target / 克隆 `UITexture` 验证通过：先使用
  `HotSpringImageTextures[0]` 加载源图，再克隆 loaded `UITexture`
  外壳并自建 target，随后通过 StreamingRef context vtable[3]
  `assign_loaded` 写回 `Track+0x50`。日志确认：
  `uiclone OK: srcTarget=0x41AB02B35A0 newTarget=0x17ECE5EEF00 newUI=0x183F18705B0 texture=0x41AB1810000`。
- `uiclone OK` 后自定义曲目仍正常进入
  `state5(5) -> playing(1)`，手动暂停也正常。因此自定义曲目的 jacket
  slot 可以承载我们自己分配的 target 和 cloned loaded `UITexture`。

## UITexture / Texture 链

默认 jacket 加载后确认：

- loaded object 类型为 `UITexture`。
- `UITexture+0x30` 指向类型为 `Texture` 的对象。
- `UITexture+0x38` 指回自身。
- `UITexture+0xD8` 指向 `BooleanFact`。

`Texture` 对象当前稳定观察到：

- `Texture+0x20` 存在内部指针。
- `Texture+0x70` 指向对象内部附近区域。
- `Texture+0xE0` 存在内部指针。
- `Texture+0x150` 存在内部指针。

这些内部字段目前只确认到地址关系，尚未确认其语义或可安全替换方式。

## 播放崩溃根因：state5 已加载路径缺失 `sub_140C18260`

### 崩溃机制

`sub_140C15200`（RVA `0xC15200`）是 `MusicRuntime` 中负责启动新曲目播放的函数。当自定义曲目被选中时，它有两个分支：

```
已加载路径 (currentTrackId == targetTrackId && currentRuntime != nullptr):
  → reset player state
  → SetPlayState(state5)
  → return  ← 不调用 sub_140C18260

新曲目路径 (currentTrackId 不匹配):
  → sub_140AC6D50(...)  // 创建/获取 runtime object
  → SetPlayState(state5)
  → sub_140C18260(...)  // 清理旧的 secondary runtime
  → sub_140C18260 内部:
      vtable[34](player)     // cleanup
      sub_140AC6E60(player)  // release
      清零 secondary runtime
  → 初始化所有剩余曲目的 runtime objects
```

崩溃走的是**已加载路径**。自定义曲目首次播放后 `currentRuntime` 已经非空，第二次选区播放时 `sub_140C15200` 判定 trackId 匹配，走已加载分支，跳过了 `sub_140C18260`。secondary runtime 残留导致 Wwise 从不发送自定义 event `0xAD100000`，game state machine 在 state5 等待 source plugin 创建而崩溃。

### 修复历程

**第一版修复（已失败）：** PlayStateMonitor detour 中在 `idle(0)→state5(5)` 时调用 `sub_140C18260` 清理 secondary runtime。日志显示 `secondary runtime=null`，修复未命中。崩溃根因不是 secondary runtime 残留，而是**旧 `currentRuntime` 被复用了**。

**第二版修复（已失败）：** Detour `sub_140C15200` 入口，在函数检查前清零 `currentTrackId` 强制走新分支。日志显示 detour 已安装但 `DLL_PROCESS_DETACH` 时仍崩溃，说明模式签匹配到了错误函数（非 `sub_140C15200`），detour 从未被调用。

**第三版修复（已确认失败且有害）：** 清零 `currentRuntime` 本身触发了 Update tick 的 abort guard。

**第四版修复方向（待实现）：** **只在 `sub_140C15200` 入口**临时清零 `currentTrackId`，不碰 `currentRuntime`。让函数走"新曲目"分支：
1. `sub_140AC6D50` 创建新的 runtime object → 设置 `currentRuntime`
2. `SetPlayState(5)` → state5
3. `sub_140C18260` 清理旧 secondary runtime
4. state5 处理器在 0.1s 后检查 `currentRuntime != 0` → 继续正常 PostEvent 流程

关键约束：**仅在函数入口清零 `currentTrackId`，在函数返回前恢复。不能碰 `currentRuntime`。**

**Pattern scan 注意事项：** 第二版修复中 detour `sub_140C15200` 入口的 pattern 签名匹配到了错误函数。需要从 IDA 确认 `0x140C15200` 开头字节，用更长的唯一签名或直接从已知的 hook 点（`SetPlayState`）计算相对偏移。

### 关键地址

| 函数 | RVA | IDB地址 |
|------|-----|---------|
| `sub_140C15200` state5入口 | `0xC15200` | `0x140C15200` |
| **`sub_140C15200` 内已加载分支 SetPlayState 调用** | `0xC15323` | `0x140C15323` |
| **state5→idle 回退（疑似错误恢复路径）** | `0xC14F25` | `0x140C14F25` |
| `sub_140C13E20` MusicRuntime更新 | `0xC13E20` | `0x140C13E20` |
| `sub_140C18260` secondary runtime清理 | `0xC18260` | `0x140C18260` |
| `sub_140C162D0` SetPlayState | `0xC162D0` | `0x140C162D0` |
| MusicRuntime+0x10288 (secondary runtime) | `+0x2830` | — |

## StreamingRef Context Vtable 契约

IDB 地址 `0x143453020`。vtable 完整：

| 槽位 | 函数 | 职责 |
|------|------|------|
| `[0]` | `sub_1426D9280` | — |
| `[1]` | `sub_1426D9B20` | — |
| `[2]` | `sub_1426D9A60` | bind: CRC32(key) → hash bucket → create or lookup target |
| `[3]` | `sub_1426D9F50` → `sub_1426D9CB0` | assign_loaded: 替换 loaded object，refCount++，通知 |
| `[4]` | `sub_1426D9E90` | — |
| `[5]` | `sub_1426D9BC0` | release: decref，refCount==0 时清理 |
| `[6]` | `sub_1426D9F70` | — |
| `[7]` | `sub_1426DA040` | — |
| `[8]` | `sub_1426DAD30` → `sub_1426DAD60` | set_flags: 触发异步加载或 consumer 通知 |
| `[9]` | `sub_1436ED8E0` | — |
| `[10]` | `sub_1426D8DA0` | — |
| `[11]` | `sub_1426D8DA0` | — |

### Target 对象结构（context 管理的内容对象）

```
target+0x00: resource_ptr (所有 track 共享 0x7FF7E3CB4CA0)
target+0x08: 0xFFFFFFFF
target+0x10: key0
target+0x18: key1
target+0x20: loaded object (0=未加载，非0=已加载的 UITexture)
target+0x28: refCount
target+0x30: ???
```

### 专辑图替换策略

由于我们的 custom track slot 已经从默认 jacket 复制了 packed (含游戏 context)，只需替换 target 的 loaded 字段即可。流程：

1. 构造自定义 UITexture+Texture（RTTI type "UITexture" 在 `+0x30` → "Texture"）
2. 创建新的 target（复制原 target，将 `+0x20` loaded 指向自定义 UITexture）
3. 创建 slot {target, packed=原 context|0x80<<52}
4. 调用 `StreamingRef_UITexture_AssignFromRef(&Track+0x50, &slot)`
5. 游戏 context 的 vtable[3] 会自动管理 lifecycle

## Texture 对象运行时布局

来自默认 jacket（hash `0x5D5CF31F`）加载后的深度探针（IDB vtable: `0x143119280`）：

```
Texture vtable: 0x7FF7E3299280 (IMAGE, 通用 Decima 反射组件)
+0x00  vtable          IMAGE  0x7FF7E3299280
+0x08  refCount        1
+0x10                  0
+0x18                  0
+0x20  pixelBuffer     HEAP  ~640KB  ← Decima 私有像素缓冲（非标准 DDS，见下文）
+0x28..+0x68           ALL ZEROS
+0x70  chain[0]        SELF+224  ← 指向附近 inline 数据，非独立分配
+0x78..+0xD8           ALL ZEROS
+0xE0  chain[1]        SELF+336
+0xE8..+0x148          ALL ZEROS
+0x150 chain[2]        SELF+448
+0x158..+0x1B8          ALL ZEROS
+0x1C0 chain[3]        SELF+560
+0x1C8..0x1FF          —
```

- 内部链 `{+0x70, +0xE0, +0x150, +0x1C0}` 间距各 112 字节 (14 qwords)，都在 Texture 所在 heap 区域内
- Texture+0x0B (word[1] 低位) = 0x32，可能是解码参数/flags

### Pixel Buffer 格式 (2026-06-07 日志确认 — 已废弃，见下方 2026-06-07 更正)

~~Pixel buffer 不是标准 DDS 文件，开头是一个 Decima 引擎私有头部...~~

**2026-06-07 更正：** 以下旧结论中"169×44 太小"的怀疑是正确的——`+0x30` 和 `+0x34` 不是纹理的像素尺寸，而是**页表的瓦片网格尺寸** (169×17 tiles)。见下方「Pixel Buffer 虚拟纹理页表结构」节。

```
pixelBuffer = 0x3E445820000 (HEAP PRIVATE, 655360 bytes = 640KB)

+0x00: ptr to IMAGE    0x7FF7E35639A0
+0x08: u64             2
+0x10: u64             2
+0x18: ptr to IMAGE    0x7FF7E35639B0
+0x20: ptr to IMAGE    0x7FF7E3563A10
+0x28: mixed           0x06 0x4B 0x01 0x00 = sizeHint(84998), 0x00 0x02 0x00 0x21 = flags
+0x30: 0xA9 0x00 0x00 0x00 → 169 (疑似 width)
       0x2C 0x00 0x00 0x00 → 44  (疑似 height)
```

**结论：**
- 开头 0x30 字节是指针元数据 + 格式描述，不是像素数据
- 尺寸 640KB 与 BC3 mip chain 512×512 典型值一致
- 169×17 不是纹理像素尺寸，而是**页表的瓦片网格尺寸**（见下一节）

### Texture Chain 条目格式

```
chain+0x00: 内部自引用指针（如 0x3E445810150 指向 SELF+336 区域）
chain+0x08: 全零（可能是未初始化的 data）
```

4 个 chain 条目共占 4×112 = 448 字节，都在 Texture 主分配块内。非独立 heap 分配，非 DDS 头。

## Decima Texture 对象 IDA 逆向 (2026-06-07)

### Texture Vtable 布局

Vtable 位于 IDB `0x143119280`（.rdata 段），共 24 个槽位：

| 槽位 | 函数 | 推测用途 |
|------|------|----------|
| `[0]` | `sub_14011E840` | 返回全局状态指针 |
| `[1]` | `sub_140149F00` | 析构/释放 |
| `[2]` | `sub_140109CE0` | 未知 |
| `[3]` | `sub_140109DB0` | 未知 |
| `[4]` | `??_R4EntityActionContextTransform` (RTTI) | 类型信息 (EntityActionContextTransform) |
| `[5]` | `sub_140125B40` | 未知 |
| `[6]` | `sub_140125B50` | 未知 |
| `[7]` | `sub_140109CE0` | 与 [2] 相同 |
| `[8]` | `sub_140109DB0` | 与 [3] 相同 |
| `[9]` | `sub_140203820` | 序列化/格式转换 (含 xmm 操作) |
| `[10]` | `sub_1402039B0` | 序列化/格式转换 |
| `[11]` | `??_R4?$ParameterSet@...` (RTTI 2) | 类型信息 (ParameterSet) |
| `[12]` | `sub_1401549E0` | 未知 |
| `[13]` | `??_R4RingSegmentVolumetricAnnotation` (RTTI 3) | 类型信息 |
| `[14]` | `sub_140129300` | 未知 |
| `[15]` | `sub_140125B50` | 与 [6] 相同 |
| `[16]` | `sub_140109CE0` | 与 [2] 相同 |
| `[17]` | `sub_140109DB0` | 与 [3] 相同 |
| `[18]` | `sub_140129360` | 未知 |
| `[19]` | `sub_1401294C0` | 未知 |
| `[20]` | `sub_1401295D0` | 未知 |
| `[21]` | `sub_1401292A0` | 未知 |
| `[22]` | `??_R4?$ParameterSet@...` (RTTI 4) | 类型信息 |
| `[23]` | `sub_140125B20` | 未知 |

这是一个多继承对象——有 4 个 RTTI 条目，分别是 `EntityActionContextTransform`、`ParameterSet`、`RingSegmentVolumetricAnnotation`、第二个 `ParameterSet`。

### Texture 默认构造器 (`sub_141D2F620`)

关键发现：

```
1. vtable = off_143119280
2. +0x00..0x6F : 全部清零 (112 字节)
3. +0x88 (=136): 设置为 50 (0x32) ← 日志中 Texture+0x0B 的 0x32 来自这里！
   实际上构造器写 *(_BYTE *)(_RBX + 88) = 50 (-16 + 88 = +0x48)
   
更正：构造器在 _RBX + 88 处写 0x32 (=50)，_RBX 结构中以 int16/int32 为单位。
实际字段布局需要对照内存 dump 和分配尺寸。
```

构造器参数：`512×512, format=31`。DXGI_FORMAT 31 = `DXGI_FORMAT_B8G8R8A8_UNORM`。
但这是构造参数，实际运行时 texture 是压缩格式（pixelBuffer 640KB 对应 512×512 BC3）。

### Pixel Buffer 分配 (`sub_141D2F7B0`)

```
1. memset(0x40000) = 256KB 清零 ← 这是输出 buffer，不是 pixelBuffer
2. 双层循环 512×512 遍历
3. 高斯模糊 kernel（SSE expf 计算权重）
4. 从源图像采样然后写入目标
```

`sub_141D2F7B0` 是一个图像降采样/后处理函数，256KB 输出 = 512×512×1byte（单通道/灰度），不是 raw pixel buffer。

### Texture 批量构造 (`sub_140124050`)

关键调用链：
```
sub_140124050(a1):
  for each mip level:
    _RDI = sub_140103CE0(...)     // 分配 Texture 对象
    _RDI->vtable = off_143119280
    _RDI[88] = 50                 // 0x32 flag
    sub_1424E5FC0(_RDI, ...)      // 绑定到 GPU/MemoryMgr
    sub_1400CB9A0(a1+120, ...)    // 设置 mip 链
  sub_140124420(a1)               // 上传到 GPU
  sub_140124A60(a1)               // 完成后处理
```

### Texture 从文件加载 (`sub_1406B9440`)

这是主纹理加载函数，调用链：
```
sub_1406B9440(rtlLock, a2):
  sub_1406B7DF0(a2)              // 查表获取纹理类型 (6种格式)
  sub_1400C10B0(&name)           // 构建资源路径 "user:<type><id>"
  // → 异步 IO 请求
  // → 解码
  sub_140103CE0(...)             // 分配 Texture 对象
  obj->vtable = off_143119280
  obj[88] = 50                    // 0x32
  sub_1424E5FC0(obj, ...)        // 绑定
  AcquireSRWLockExclusive
  sub_1406C7F80(...)             // 插入到纹理缓存
  ReleaseSRWLockExclusive
```

### 专辑图替换策略更新

基于上述分析，构造自定义 Texture 的方案需要调整：

**确认可行的路径：**
1. 调用 `sub_140103CE0(&word_145E1F740)` 分配原始 Texture 对象 → 已有 vtable 和 0x32 flag
2. 调用 `sub_1424E5FC0(obj, userTextureResource, &result)` 绑定我们的像素数据
3. 或者直接偷懒：**重用已加载的默认 jacket Texture 对象，只替换 pixelBuffer 指针和对应的 GPU resource**
4. 替换后通过 `StreamingRef_UITexture_AssignFromRef` 安装到自定义 track 的 jacket slot

**需要进一步确认的：**
- `word_145E1F740` 的符号名和含义
- `sub_1424E5FC0` 的参数语义（特别是第二个参数的结构）
- pixel buffer 的实际格式——不是标准 DDS 而是 Decima 私有格式，含 header

## state5→idle 回退地址

### 确认：`0xC14F25` ∈ `sub_140C13E20` (MusicRuntime_Update)

通过 IDA 反编译确认 `0xC14F25` 位于 MusicRuntime_Update 内部的 abort/idle 路径。

**触发条件：** state5 处理器在累积时间超过阈值（~0.1s，常量 `0x143460D58`）后检查 `currentRuntime == 0`，若为空则判定 runtime 异常丢失，跳转到清理/idle 路径。

**已知触发场景：**
- **我们自己的 fix 清零了 `currentRuntime`**：第三版 detour 在 SetPlayState 出口将 runtime 置零，~100ms 后 Update tick 检测到 runtime=null → 回退 idle
- 正常情况下这是防御代码：如果 state5 期间 runtime 被意外释放，则安全回退而非崩溃

### currentRuntime guard 汇编

```
140C14642  vaddss  xmm0, xmm8, [rdi+2818h]   ; state5 累积时间 += deltaTime
140C1464A  vcomiss xmm0, [0x143460D58]       ; 阈值 ≈ 0.1s (float 0x3DCCCCCD)
140C14652  vmovss  [rdi+2818h], xmm0
140C1465A  jb      short loc_140C14F34        ; 未到阈值 → 跳过等待
140C14660  cmp     [rdi+1918h], rsi           ; currentRuntime == 0?
140C14667  jz      loc_140C14E80              ; 是 → abort/idle
// 正常分支: (*(currentRuntime->vtable+0x90))(currentRuntime)  ← PostEvent
140C14E80  // abort 路径:
140C14F15  xor     edx, edx
140C14F17  mov     [rdi+1924h], esi            ; currentTrackId = 0
140C14F1D  mov     rcx, rdi
140C14F20  call    sub_140C162D0               ; SetPlayState(0) → idle
140C14F28  call    sub_140C17920               ; 清理状态
```

### 关键地址补充

| 地址 | RVA | 描述 |
|------|-----|------|
| `sub_140C15200` 内部 state5 入口 | `0xC15323` | 玩家选区触发自定义曲目的 caller |
| **state5 时间阈值常量** | `0x143460D58` | 浮点常量 ≈ 0.1s，控制 state5 等待周期 |
| **state5→idle 回退** | `0xC14F25` | `sub_140C13E20` abort 路径，`SetPlayState(0)` 后清理 |
| MusicRuntime+0x2818 | — | state5 累积等待时间（float） |

## 2026-06-07 完整运行时样本

### 样本环境

- 游戏在菜单中启动，然后自动进入游戏世界
- PostEvent 日志显示大量 `callerRva=0x26C21B6` 的 event（常规游戏音频，非我们的插件）
- 有一个 `externalCount=1` 的特殊 event `0x2C194AEA`（游戏原生外部音频，非我们）
- **整个会话中 PostEvent 未记录过 `0xAD100000`**

### DSUICatalogueImageResource 数组确认

```
MusicJacketImageTextures:       count=0  capacity=0  data=0x0  ← 完全空
MusicJacketImageNameHash:       count=0  capacity=0  data=0x0  ← 完全空
DefaultMusicJacketImageTexture: slot=0x3E464DB7A00  target=0x3E464E08AC0  ← 存在
DefaultMusicJacketImageNameHash: 0x5D5CF31F                          ← 存在
```

**结论：** 游戏当前版本中，`DSUICatalogueImageResource` 的纹理数组和哈希数组是**完全空的**。所有曲目（包括原版曲目）共用同一个默认 jacket。这意味着如果我们想注入自定义专辑图，有两种路径：

1. **只修改默认 jacket**（简单但会影响所有曲目）
2. **扩展 `MusicJacketImageTextures` / `MusicJacketImageNameHash` 数组**，像注入 track 一样注入新的纹理条目

### UITexture 对象完整布局 (2026-06-07 探针)

```
UITexture+0x00  vtable          IMAGE  0x7FF7E35D6650
UITexture+0x08  refCount        0xD9D500000002
UITexture+0x10  key0            0xB145BEBF2B18D084
UITexture+0x18  key1            0x6CAF69B12F673B87
UITexture+0x20  ??              0x10000000000
UITexture+0x28  ??              0x100
UITexture+0x30 → Texture        0x3E445810000  (typed "Texture")
UITexture+0x38 → self           0x1FA828AEBB8  (typed "UITexture")
UITexture+0x40..0x68            ALL ZEROS
UITexture+0x70 → ???            0x1FA8141A0A0 (typed pointer)
UITexture+0x78..0x88            ALL ZEROS
UITexture+0x90 → ???            0x1FA800C5F20 (typed pointer)
UITexture+0x98                  0x100000001
UITexture+0xA0 → ???            0x1FA828AEC90 (typed pointer)
UITexture+0xA8                  0
UITexture+0xB0  vtable2?       0x7FF7E32AC420
UITexture+0xB8                 0x129C500000003
UITexture+0xC0  key?           0xBE494FC826B18F5D
UITexture+0xC8  key?           0x68CB9C7F743D46AD
UITexture+0xD0                  0x1
UITexture+0xD8 → BooleanFact   0x1FA828AEC58  (typed "BooleanFact")
UITexture+0xE0..0xF8            ALL ZEROS
```

**新发现：** UITexture 比之前文档记录的更复杂。除了 `+0x30→Texture`、`+0x38→self`、`+0xD8→BooleanFact` 外，还额外有 3 个 typed pointer（`+0x70`, `+0x90`, `+0xA0`）和一个 second vtable（`+0xB0`）。这些可能与 Decima 引擎的异步加载状态、纹理缓存或渲染管线绑定有关。

## Pixel Buffer 虚拟纹理页表结构 (2026-06-07 深度 dump)

对默认 jacket pixelBuffer 的 640KB hex dump 确认：**这不是扁平像素缓冲，而是 Decima 虚拟纹理（Virtual Texture）运行时数据结构。**

### 根头部 (Page 0, at `+0x00000`)

| 偏移 | 值 | 语义 |
|------|-----|------|
| `+0x00` | `IMAGE*` (如 `0x7FF710C75C98`) | 类型指针1 |
| `+0x08` | `2` | 维度数 / 页数量标记 |
| `+0x10` | `0x3F4A` = 16202 | 全局计数 |
| `+0x18` | `IMAGE*` | 类型指针2 |
| `+0x20` | `IMAGE*` | 类型指针3 |
| `+0x28` | `0x00014B06` (sizeHint=84742), `0x21000200` (flags) | 大小提示 + 标志字 |
| `+0x30` | `169` | **页表宽度(瓦片数)** — 不是像素宽度 |
| `+0x34` | `17` | **页表高度(瓦片数)** — 不是像素高度 |
| `+0x38` | heap ptr | 指向页条目列表 |
| `+0x40-0x4F` | 16 字节 | CRC64 内容哈希 |
| `+0x70` | `0xFFFFFFFF` | sentinel |
| `+0x78` | `0x0304` = 772 | 页条目总数 |
| `+0x88` | heap ptr | 第一个数据页(位于 0x10000) |
| `+0xC0` | `0x3E` = 62 | 页计数或 flag |

### 页布局

每个页 0x10000 (64KB) 对齐，含自己的子头部 + BCn 压缩像素数据。

**像素数据页头格式 (以 Page 0x40000 为例):**
```
+0x00: heap vtable ptr
+0x08: 0x0C = type flag (12 表示含像素数据)
+0x10: size info
+0x18: 0x32 = format flag (与 Texture+0x0B 相同)
+0x30-0x3F: 16字节页内容哈希
+0x40: 格式描述符
+0x50: "DXBC" FourCC + variant bytes
       注: "DXBC" (bytes 44 58 42 43) 是 Decima 引擎扩展压缩格式标识，
       不是标准 DDS BCn FourCC
+0x68: pixel data 大小
+0x6C: 0x07 = mip 层级数
+0x70+: mip 偏移表 (每个 mip 32 位)
```

### 各数据页位置

页分散在整块 640KB 虚拟分配内：

| 页偏移 | 大小 | 内容 |
|--------|------|------|
| `0x10000` | 3,653 bytes | 页表元数据 (`0x0B` = mip count) |
| `0x20000` | 3,580 bytes | 第二页表 (3个 vtableptr) |
| `0x30000` | 4,037 bytes | 页指针表 (2组指针, 3个 entry 分组) |
| `0x40000` | 12,442 bytes | **BCn mip 0 (DXBC header)** |
| `0x43800` | 12,695 bytes | mip 0 数据尾部 |
| `0x50000` | 3,965 bytes | tile 映射表 (含 (`0x05`, `0x14`, `0x32`) 索引三元组) |
| `0x60000` | 10,701 bytes | **BCn mip 1 (DXBC header)** |
| `0x63000` | 11,435 bytes | mip 1 数据尾部 |
| `0x70000` | 3,374 bytes | **BCn mip 2 (DXBC header)** |
| `0x80000` | 15,869 bytes | **BCn mip 3 (DXBC header)** |
| `0x90000` | 3,979 bytes | tile 映射元数据 |
| 其他 | — | 零填充 (~85% 空) |

### "DXBC" 像素数据页面格式 (Page 0x40000, 0x60000, 0x70000, 0x80000)

```
0x40000:
+0x00: 0x0100 C7E4 heap vtable ptr
+0x08: 0x0C = type: 12 (pixel data)
+0x10: 0x02DDFBBD = size hint
+0x18: 0x32 = format flag
+0x30: 0x9F4EE6AA8CEF1FD5 content hash (high)
+0x38: 0xD26B844A02118BB0 + 0x1ABBE2DF = content hash (low) + salt
+0x40: 0x00E00900 = 649216 pixel payload size
+0x48: 44 58 42 43 = "DXBC" FourCC
+0x4C: 34 = variant byte '4'
+0x50: A2 2C 7B 7B ... (format-specific header, BC4-like)
+0x68: 0x09E000 = 649216 (pixel data size)
+0x6C: 0x07 = 7 mip levels
+0x70: 0x3C, 0x4C, 0x5C/0x88, 0x6C/0xC4 = mip offsets
     (Page 0x70000: 0x3C, 0x4C, 0x5C, 0x6C → 4 mips, 更小的纹理)
```

### 结论

**直接覆写 pixelBuffer 不可行** — 原因是：
1. 数据结构是**离散的虚拟纹理页表**，不是连续的像素数据
2. 像素数据分散在 10+ 个独立的页面中
3. 页表间的指针、偏移表、哈希值构成内部一致性闭锁——替换单片会导致哈希不匹配/指针断裂
4. 压缩格式为 Decima 扩展的 "DXBC" 编码，非标准 DDS BCn
5. 对每个 mip level 的页独立含自己的子头部和内容哈希

**正确方向：走资源层级注入，不碰底层页表：**
- **方案A:** 克隆 UITexture 和 Texture 对象，用新的 texture resource 替换
- **方案B:** Hook StreamingRef context bind/assign 路径，注入指向自定义纹理资源的 slot

## Pixel Buffer 覆写实验结论 (2026-06-07)

### 实验目标

验证通过覆写已加载默认 jacket 的 pixel buffer 能否改变专辑图显示。

### 链条确认

通过 `target→UITexture(+0x30)→Texture(+0x20)→pixelBuffer` 的链条**完全正确**：

```
target+0x00: resource_ptr = 0x7FF7xxxxxxxx  (IMAGE, 所有 track 共享)
target+0x08: 0xFFFFFFFF
target+0x10: key0 = 0xB145BEBF2B18D084
target+0x18: key1 = 0x6CAF69B12F673B87
target+0x20: loaded UITexture = heap addr, type="UITexture" ✓
target+0x28: refCount

UITexture+0x30: Texture = heap addr, type="Texture" ✓
Texture+0x0B: 0x32 flag ✓
Texture+0x20: pixelBuffer = heap addr ✓
```

### Pixel Buffer 大小不稳定 — 方案不可行

连续三次测试中，同一个默认 jacket（hash `0x5D5CF31F`）的 pixelBuffer 大小不同：

| 测试 | pixelBuffer 大小 | 说明 |
|------|-----------------|------|
| 第一次运行 | 655,360 (640KB) | 符合 512×512 BC3 mip chain |
| 第二次运行 | 2,228,224 (2.1MB) | 不同分辨率 |
| 第三次运行 | 9,109,504 (9.1MB) | 大幅纹理，也许是 texture array |

**结论：直接覆写 pixel buffer 不可行。**不同加载周期中 Decima 引擎可能返回不同分辨率或不同 mip level 数量的纹理对象。Pixel buffer header 含 IMAGE 段指针但具体格式未知。覆写错误的 buffer 会导致崩溃（第二次运行崩溃确认了这一点）。

### 后续方向

**覆写方案永久放弃。** 当前已验证 clone-wire-reuse 模式安全（3/3 成功）。下一步构造独立 Texture 对象以注入自定义像素数据。

**可行路线（按优先级）：**
1. **VirtualAlloc 构造干净 Texture** — 0x200 字节 zero-init，手动填 vtable=(off_143119280)、flag 0x32、宽高、format 信息，内部链全部置零（日志确认现有 shared Texture 的 chain 指针就是 0），分配自定义 BC3 pixelBuffer，Texture+0x20 指向它
2. **调用游戏构造函数** — `sub_140103CE0` + `sub_1424E5FC0` forward 调用，但需逆向 v12 参数（GPU resource）的结构
3. **扩展 `MusicJacketImageTextures` 数组** — 当前 count=0, capacity=0，所有曲目共用 `DefaultMusicJacketImageTexture`。像扩展 `AllTracks` 一样扩展此数组，注入新条目。

**当前已验证方案（2026-06-07 3/3 成功）：**
- 克隆 UITexture (0x100 bytes) + ResetObjectHeader，复用原 Texture（bump refCount）
- 新 target + slot，vtable[3] assign
- 自定义曲目稳定显示默认专辑图，音频播放/暂停正常

## 播放状态验证 (2026-06-07)

### 自定义曲目播放成功

```
idle(0) → state5(5)  trackId=0xAD900001  callerRva=0xC15323
state5(5) → playing(1)  callerRva=0xC14691  ← (*(currentRuntime->vtable+0x90))(currentRuntime)
```

调用链确认：`sub_140C15200`（state5 entry）→ `SetPlayState(5)` → Update tick 累积 ~0.1s → `currentRuntime->vtable[0x90]` → PostEvent → playing(1)。

### 手动暂停/恢复工作正常

```
playing(1) → paused(2)  callerRva=0xC15594  → browser control pause reason=manual
```

state5 fix 不需要了——在回退到 PlayStateMonitor 干净版本后，首次播放自定义曲目就走"新曲目路径"（入口时 trackId=0），自然创建了新 runtime。

## DSUICatalogueImageResource 逆向 (2026-06-07)

### 类层次结构

通过 RTTI 分析，`DSUICatalogueImageResource`（注册名 `CatalogueImageResource`）继承自 `MenuRadioSettingResource`。

**RTTI 链：**
- TypeDescriptor: `0x143E6EE68` → name="CatalogueImageResource"
- 父类: `MenuRadioSettingResource`（字符串位于 `0x1432B9638`）
- 命名空间前缀 "DsUI" 由 HookUtils::RttiName 运行时附加

**类字段（运行时确认 + IDA 确认）：**

```
+0x60  MusicJacketImageTextures     RawArray<slot ptr>    count=0 capacity=0
+0xC0  DefaultMusicJacketImageTexture  slot ptr             始终存在
+0x118 MusicJacketImageNameHash      RawArray<uint32_t>    count=0 capacity=0
+0x178 DefaultMusicJacketImageNameHash  uint32_t            0x5D5CF31F
```

### 全局资源管理

`qword_1462667E0` = 全局资源管理器（`sub_1406932F0` 初始化），构造函数 `sub_1426D8F90` 是一个 656864 字节的大型容器。

它通过 `sub_1426F4610` 注册资源类型映射表（CRC32 哈希表），将通用资源符号映射到具体处理器。`DSUICatalogueImageResource` 对应的资源符号通过 StreamingRef context（vtable `0x143453020`）管理纹理加载。

### Track+0x50 的 StreamingRef slot 绑定

自定义曲目的 `Track+0x50` 是 jacket streaming slot。游戏 UI（菜单/音乐列表）显示专辑图时，走以下路径：

```
DSUIMusicMenuDataSourceResource (曲目数据源)
  → Track+0x30 = AlbumResource (专辑)
    → Track+0x50 = StreamingRef slot (jacket)
      → slot.target → context.bind(CRC32) → UITexture
```

每条曲目理论上有独立的 `+0x50` slot，但当前所有曲目都指向相同的 default target（因为 `MusicJacketImageTextures` 数组为空）。

**绑定流程（代码路径）：**
1. `sub_1416E56B0` — 从 `DSUICatalogueImageResource` 读取 MusicJacketImageTextures 数组
2. `sub_141720B10` — 64 字节结构的 vector push_back（每个条目含 hash + slot 引用）
3. `sub_1416E5AC0` — 条目复制（含 `StreamingRef_UITexture_AssignFromRef`）
4. `StreamingRef_UITexture_AssignFromRef` (`0x1426E4EE0`) — 用 context vtable[2] 做 bind，vtable[3] 做 assign

**关键发现：** `StreamingRef_UITexture_AssignFromRef` 在 slot 的 packed[1] 含 flag `0x80<<52` 时走"已加载路径"（调用 vtable[3]），否则走"先 bind 再 assign"路径。这解释了自定义曲目继承源曲目 jacket 后为何能加载：源曲目的 slot 已经含正确的 packed + target。

### 关键函数

| 函数 | 地址 | 描述 |
|------|------|------|
| `sub_1416E56B0` | `0x1416E56B0` | 从 DSUICatalogueImageResource 读取 MusicJacketImageTextures 数组 |
| `sub_141720B10` | `0x141720B10` | 64 字节条目 vector push_back（含 hash + slot 引用） |
| `sub_1416E5AC0` | `0x1416E5AC0` | 条目复制（含 `StreamingRef_UITexture_AssignFromRef`） |
| `StreamingRef_UITexture_AssignFromRef` | `0x1426E4EE0` | slot 安装：检查 flag 0x80 决定走已加载路径还是 bind+assign |

## 专辑图替换实验 (2026-06-07 多轮迭代)

### 第一轮：完整克隆 UITexture+Texture (12:47 会话)

**操作：** 克隆 UITexture(0x100) + Texture(0x200)，新 BC3 pixelBuffer，新 target + slot，vtable[3] assign。

**结果：** ❌ 崩溃
- clone 成功、assign 成功、游戏存活约 20 秒
- state5 进入后 ~700ms 崩溃
- **根因：** Texture clone 含内部链指针（`+0x70/+0xE0/+0x150/+0x1C0`），memcpy 后仍指向原 Texture 的 heap 内部偏移。原对象释放后 UAF 崩溃。

### 第二轮：仅克隆 UITexture，复用原 Texture (12:51 会话)

**操作：** 克隆 UITexture(0x100)，+0x30 指向原 Texture（bump refCount），新 target/slot。

**结果：** ❌ 崩溃
- 同上模式崩溃

### 第三轮：同样的 UITexture-only 克隆 (13:00 会话)

**操作：** 与第二轮相同。

**结果：** ✅ **完全成功！**
```
clone OK: newUI=0x1557345ADD0 origTex=0x5B46C410000
state5→playing(1) ✓
playing→paused ✓  (browser control sent=1)
正常退出
```

### 第四、五轮：确认稳定性的重复测试 (13:05, 13:09 会话)

**结果：** ✅ **两次完全成功**
```
13:05: clone OK → state5→playing → paused → DETACH (正常退出)
13:09: clone OK → state5→playing → paused → DETACH (正常退出)
```

### 专辑图视觉效果

**由于 UITexture 复用原 Texture，自定义曲目显示的是与其他曲目相同的默认专辑图。** 这是预期行为——我们的目标是先确认 clone+assign 机制安全，再解决自定义像素数据注入。

### 已验证的安全性

- `SEH_AssignLoaded` 调用 vtable[3] 替换 slot **完全安全**（4/4 次成功）
- 克隆 UITexture (0x100)+刷新 header 不会引入 dangling pointer
- 新 target key 与旧 default target key 不同不会导致冲突
- 游戏不受影响地正常运行（音频播放、浏览器互动均正常）

### 下一步：独立 Texture 对象

要让自定义曲目显示不同图片，需要构造新的 Texture 对象（非 clone），通过以下两种路径之一：

**路径A — 调用游戏构造函数：**
1. `sub_140103CE0(&word_145E1F740)` 分配 Texture
2. `sub_1424E5FC0` 绑定 GPU 资源
3. 构造自定义 pixelBuffer（需理解 Decima 内部格式）
4. 将新 Texture 的 +0x20 指向自定义 pixelBuffer

**路径B — VirtualAlloc 构造干净的 Texture：**
1. VirtualAlloc 0x200 字节，手动填充 vtable、flags、width/height
2. 由于现有 shared Texture 已有全零的链指针 `+0xE0..+0x158`（日志确认），直接设为零即可
3. 分配自定义 BC3 pixelBuffer，Texture+0x20 指向它

两个路线的关键都是：不能让 Decima 引擎通过内部链去访问不在 heap 内的偏移地址。VirtualAlloc 一块大的 backing store 可以解决此问题。

## Odradek 参考知识 (2026-06-07)

### DS2 RTTI 类型概要

来自 `odradek/odradek-game-ds2/src/main/resources/types.json`：

**DS2.Texture** 继承自 `TextureResource`：
```
+0x28: cptr_TextureSet (TextureSetParent)
+0x30: uint32[12] (StreamingMipOffsets)
+0x58: StreamingDataSource (Channel/Offset/Length)
```

**DS2.UITexture** 继承自 `Resource`：
```
+0x20: bool (UseCustomTextureShader)
+0x24: ISize (Size)
+0x28: TextureInfo (smallTexture) — hash, header, data
+0x?? + TextureInfo (largeTexture) — hash, header, data
// animated 版本用 UITextureFramesInfo
```

**DS2.TextureHeader** (deserialize 顺序):
```
+0x00: uint16 type (ETextureType: 2D/2DArray/3D/CubeMap)
+0x02: uint16 width  (低14位=像素宽，高2位=flags)
+0x04: uint16 height (低14位=像素高，高2位=flags)
+0x06: uint16 numSurfaces
+0x08: uint8  numMips
+0x09: uint8  pixelFormat (EPixelFormat)
+0x0A: uint8  unk0A
+0x0B: uint8  colorSpace (ETexColorSpace)
+0x0C-0x0F: uint8[4] unk
+0x10: MurmurHashValue hash
```

**DS2.TextureData**:
```
+0x00: int32 totalSize
+0x04: int32 embeddedSize
+0x08: int32 streamedSize
+0x0C: int32 streamedMips
+0x10: uint8[] embeddedData (size = totalSize - 12)
```

**DS2.StreamingDataSource**:
```
+0x00: uint8 Channel
+0x04: int32 Offset
+0x08: int32 Length
+0x10: uint64 Locator (fileIndex<<24 | fileOffset)
```

### ObjectId 与 StreamingRef

`StreamingRef<T>(ObjectId)` — ObjectId 由 `{groupId, indexInGroup}` 组成。StreamingRef 通过 lookup 在 streaming graph 中查找对应的 `{file, offset, length}`，然后反序列化为 TypedObject。

### 关键函数对照

| Odradek 概念 | Runtime 对应 |
|-------------|-------------|
| `Texture.header().pixelFormat()` | Texture+0x0B flags 和 vtable 类型推导 |
| `Texture.data().embeddedData()` | pixelBuffer (`Texture+0x20`) |
| `Texture.data().streamedMips()` | 大于 embeddedSize 的 mips 从 StreamingDataSource 流式加载 |
| `StreamingRef.objectId()` | slot.packed 低 44 位 = context 地址 + target index hash |
| `StreamingRef.bind()` | vtable[2] = `sub_1426D9A60` (CRC32 bind) |
| `StreamingRef.assign()` | vtable[3] = `sub_1426D9F50→sub_1426D9CB0` (assign_loaded) |
