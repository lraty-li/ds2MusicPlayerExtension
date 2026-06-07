# 自定义专辑图实现方案

## 背景

音频播放已完全正常。`CustomJacketPixelTest.cpp` 当前将默认 jacket 的 slot 复制给自定义曲目，显示的是与其他曲目相同的默认专辑图。

目标：让自定义曲目显示**不同的自定义图片**。

## 当前链条

```
Track+0x50 → slot {target, packed} → target {resource, key0, key1, loaded(UITexture), refCount}
  → UITexture(+0x30) → Texture(+0x20) → pixelBuffer
```

`StreamingRef_UITexture_AssignFromRef` (0x1426E4EE0) 负责安装 slot：
- packed 有 `0x80<<52` 标志 → 调 vtable[3] (assign_loaded) 直接替换 loaded 对象
- 无标志 → 调 vtable[2] (bind) 通过 CRC32 查找或创建 target，然后 vtable[8] 触发异步加载

## 方案：克隆默认 Texture + 替换 Pixel Buffer

核心思路：游戏已经加载了一个完全可用的默认 jacket (UITexture+Texture+pixelBuffer)。我们克隆这个链条，但替换 pixel buffer 内容为自定义图片数据，然后创建新 target/slot 安装到自定义曲目。

### 优势
- 不需要理解 Decima 引擎的完整纹理加载流程
- 不需要调用未知参数语义的游戏内部函数
- 利用已经在工作的对象结构

### 挑战
- Decima pixel buffer 格式非常规 DDS，含自定义头部
- 需要在运行时探测格式（尺寸、压缩类型、头部结构）
- 跨运行 pixel buffer 尺寸可能不同（之前观察到 640KB/2.1MB/9.1MB）

## 实现步骤

### Phase 1: 运行时 Pixel Buffer 格式探测

在当前 jacket probe 线程中扩展探测，不再只读 `target+0x20`(loaded)，而是深入读取 pixel buffer 的元数据：

```
Texture+0x20 → pixelBuffer 地址
pixelBuffer+0x00 → type指针 (IMAGE段)
pixelBuffer+0x08 → 2 (未知u64)
pixelBuffer+0x10 → 2 (未知u64)  
pixelBuffer+0x18 → type指针 (IMAGE段)
pixelBuffer+0x20 → type指针 (IMAGE段)
pixelBuffer+0x28 → flags/sizeHint
pixelBuffer+0x30 → width (u32)
pixelBuffer+0x34 → height (u32)
pixelBuffer+0x38 → depth/mips (u32?)
pixelBuffer+0x3C → format (u32?) — DXGI_FORMAT
pixelBuffer+0x40+ → 实际像素数据起始
```

需要确认的字段：
1. 像素数据起始偏移（头部大小）
2. 纹理实际宽高
3. 压缩格式 (BC1/BC3/BC7)
4. Mip level 数量
5. 每个 mip level 的数据偏移和大小

一旦头部结构确定，我们就可以：
- 从 PNG 加载自定义图片
- 编码为相同的压缩格式
- 构造匹配的 mip chain
- 复制原始头部 → 新 buffer
- 替换克隆 Texture 的 pixelBuffer 指针

### Phase 2: 链克隆与替换

```cpp
// 1. 从默认 jacket 获取已加载的 UITexture
void* defaultLoaded = *(void**)(g_target + 0x20);  // UITexture*
void* defaultTexture = *(void**)((uint8_t*)defaultLoaded + 0x30);  // Texture*

// 2. 探针 pixel buffer
void* defaultPixelBuffer = *(void**)((uint8_t*)defaultTexture + 0x20);
size_t defaultPixelBufferSize = /* 从 heap 信息获取，或从 header 计算 */;

// 3. 克隆 Texture 对象 (memcpy, size ≈ 0x200)
void* clonedTexture = HeapAllocZero(textureObjSize);
memcpy(clonedTexture, defaultTexture, textureObjSize);
ResetObjectHeader(clonedTexture);

// 4. 构造自定义 pixel buffer
//    - 复制原始 header
//    - 加载 PNG → decode RGB → BC3 压缩 + mip chain → 追加到 header 后
void* customPixelBuffer = VirtualAlloc(nullptr, defaultPixelBufferSize, 
                                        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
memcpy(customPixelBuffer, defaultPixelBuffer, headerSize);  // 复制头部
EncodeCustomImage(customPixelBuffer + headerSize, pngPath, width, height, format);

// 5. 替换克隆 Texture 的 pixelBuffer
*(void**)((uint8_t*)clonedTexture + 0x20) = customPixelBuffer;

// 6. 克隆 UITexture
void* clonedUITexture = HeapAllocZero(uiTextureSize);
memcpy(clonedUITexture, defaultLoaded, uiTextureSize);
ResetObjectHeader(clonedUITexture);
*(void**)((uint8_t*)clonedUITexture + 0x30) = clonedTexture;  // 更新 Texture 指针

// 7. 创建新 target
Target newTarget = {};
newTarget.resource_ptr = originalTarget->resource_ptr;  // 共享 resource
newTarget.key0 = /* 新 hash */;
newTarget.key1 = /* 新 hash */;
newTarget.loaded = clonedUITexture;
newTarget.refCount = 1;

// 8. 创建新 slot
Slot newSlot = {&newTarget, originalSlot.packed | (0x80ULL << 52)};

// 9. 安装
StreamingRef_UITexture_AssignFromRef(&Track+0x50, &newSlot);
```

### Phase 3: 图片编码

需要一个轻量级 BCn 编码器。选项：
- `bc7enc` / `bc7e` (MIT) — 高质量 BC7
- `DirectXTex` (MIT) — 完整 DDS 工具链
- `stb_image` + 手动 BCn 编码 — 最简

推荐使用 `DirectXTex` 库，它可以直接加载 PNG 并输出各种 BCn 格式。

## 需要进一步逆向的内容

### 1. Pixel Buffer Header 完整布局

从 IDA 中分析 `sub_1406B9440` (纹理从文件加载) 中 pixel buffer 构造部分的汇编，确定头部各字段的确切语义。

### 2. Texture 对象分配尺寸

通过 `sub_140103CE0(&word_145E1F740)` 分配的对象大小 — 需要确定这个值以正确 memcpy 克隆 Texture。

### 3. UITexture 对象分配尺寸

UITexture 有复杂的内部结构（多个 typed pointer、second vtable）。需要确定精确的分配大小。

### 4. 图片文件路径

自定义图片需要放在游戏能访问的位置，建议：
- `scripts\ds2_music_player_jacket.png` 
- 在 ASI 中硬编码路径

## 备选方案：跳过 pixel buffer，直接 hook GPU 绑定

如果 pixel buffer 格式过于复杂无法在合理时间内逆向，可以：

1. 克隆 UITexture+Texture（保留原始 pixel buffer，先用默认图）
2. 在 Texture 绑定到 GPU 时 (`sub_1424E5FC0`) hook 替换为我们用 DirectX 创建的 GPU 纹理
3. 优势：可以完全用标准 DDS 格式，不需要理解 Decima pixel buffer 头部

这需要 hook `sub_1424E5FC0` 或者直接调用 DirectX 创建纹理后替换 Texture+0x20 指针指向我们的标准格式纹理。

## 关键 IDA 地址

| 函数 | 地址 | 用途 |
|------|------|------|
| `sub_140103CE0` | `0x140103CE0` | 分配 Texture 对象 |
| `sub_1424E5FC0` | `0x1424E5FC0` | 绑定 GPU/MemoryMgr 资源 |
| `sub_1406B9440` | `0x1406B9440` | 从文件加载纹理 |
| `sub_1406C7F80` | `0x1406C7F80` | 纹理缓存插入 |
| `sub_140109C80` | `0x140109C80` | 对象释放 (RefCount --) |
| `StreamingRef_UITexture_AssignFromRef` | `0x1426E4EE0` | Slot 安装 |
| `sub_1426D9CB0` | `0x1426D9CB0` | vtable[3] assign_loaded |
| `sub_141D2F620` | `0x141D2F620` | 默认 Texture 构造 (512×512, RGBA8) |
| `sub_141D2F7B0` | `0x141D2F7B0` | 高斯模糊后处理 |
| `sub_140124050` | `0x140124050` | Texture 批量构造 (含 mip chain) |
| `off_143119280` | `0x143119280` | Texture vtable |
| `word_145E1F740` | `0x145E1F740` | Texture 分配器类型标识 |

## 当前日志中的关键数据

```
custom jacket applied: target=0x308E0259DE0 key0=0xB145BEBF2B18D084 key1=0x6CAF69B12F673B87
jacket tick=1 loaded=0x2B8428AEBB8  ← UITexture 加载成功
```

`loaded` = UITexture 地址，`UITexture+0x30` = Texture 地址，`Texture+0x20` = pixelBuffer 地址。
