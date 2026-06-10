# 专辑图知识库

本文只记录 DS2MusicPlayer 自定义曲目专辑图相关的已验证知识。

## 目标

外部/自定义曲目必须能够显示自定义专辑图。已验证到：

- 外部曲目可以显示自有 D3D12 texture resource 中的合法 BC7 测试图。
- 专辑图替换的关键不是音频播放或 metadata，而是让可见 `UITexture -> Texture ->
  TextureDX12` 链路指向自有 GPU resource/view。

## 可见对象链

```text
Track+0x50
  -> StreamingRef_UITexture slot { target, packed }
    -> target+0x20 loaded UITexture
      -> UITexture+0x30 Texture
        -> Texture+0x20 TextureDX12 / pixelBuffer
```

已验证：

- 替换 `Track+0x50` 的 `StreamingRef_UITexture` slot 会改变音乐菜单可见专辑图。
- `target+0x20` 是已加载的 `UITexture`。
- `UITexture+0x30` 是可见 `Texture` 指针。
- `Texture+0x20` 是可见 `TextureDX12` / pixelBuffer 指针。
- 自建 target、cloned `UITexture`、cloned `Texture` 可稳定用于外部曲目。

## 可用控制素材

- `DefaultConstructionHoloImageTexture` 是可加载的系统占位图，视觉为 `NO DATA`。
- `HotSpringImageTextures[0]` 是可加载的真实游戏素材，用户视觉观察为苹果图。
- catalogue 的 `Array_StreamingRef_UITexture` 条目可直接作为 `Track+0x50` 的替换来源。

## StreamingRef 契约

`Track+0x50` 持有 `StreamingRef_UITexture` slot。slot 指向 context 管理的 target。

target 已确认字段：

```text
target+0x00: resource pointer
target+0x08: 0xFFFFFFFF
target+0x10: key0
target+0x18: key1
target+0x20: loaded object, 非空时为 UITexture
target+0x28: refCount
```

可用安装方式：

- 初始安装会把 catalogue slot 复制到 `Track+0x50`，增加 target refCount，
  再通过 context vtable `[8]` 触发加载。
- 构造自有 target，并让 `target+0x20` 指向 cloned `UITexture`。
- 通过 `StreamingRef_UITexture` context vtable `[3]` / `assign_loaded` 把新 slot 写入
  `Track+0x50`。
- 外部曲目播放、暂停、恢复期间该链路稳定。

旧静态分析确认：`StreamingRef_UITexture_AssignFromRef` 会根据 packed 标志选择
直接 `assign_loaded` 或 bind/lookup + trigger load 路线。已验证 clone
安装可使用 vtable `[3]`，加载触发可使用 vtable `[8]`。

## UITexture 与 Texture

已验证字段：

```text
UITexture+0x30: Texture*
Texture+0x20: TextureDX12 / pixelBuffer*
```

控制结论：

- 只替换 cloned `UITexture+0x30` 为 `NO DATA` 的原生 `Texture`，可见图变为
  `NO DATA`。
- 只替换 cloned `Texture+0x20` 为 `NO DATA` 的原生 `TextureDX12` /
  pixelBuffer，可见图变为 `NO DATA`。
- 因此专辑图显示由 `UITexture+0x30 -> Texture+0x20` 链路控制。

已验证 clone 结构约束：

```text
cloned UITexture: object copy size 0x100, object header must be reset
cloned Texture:   storage size 0x600, source Texture header copy size 0x70
Texture 内部链:   +0x70 -> +0xE0 -> +0x150 -> +0x1C0 -> 0
loaded target:    target size 0x30, target+0x20 = cloned UITexture, target+0x28 = 1
```

## TextureDX12 关键字段

`Texture+0x20` 指向的对象在本任务中按 `TextureDX12` / pixelBuffer 处理。

已验证关键字段：

```text
TextureDX12+0x78: 24-byte main resource handle slot
TextureDX12+0x88: main wrapper pointer, wrapper-backed slot 的 q10
TextureDX12+0x90: main descriptor/view block
TextureDX12+0xC8: 24-byte secondary resource handle slot
TextureDX12+0xD8: secondary wrapper pointer
TextureDX12+0xE0: secondary descriptor/view block
```

wrapper-backed handle slot 形态：

```text
[0x304, 0, wrapper]
```

wrapper 已验证字段：

```text
wrapper+0x08: engine / D3D12 resource object
wrapper+0x30: CPU pixel data pointer
```

重要结论：

- memcpy / VirtualAlloc 复制 pixelBuffer 后，即使 `wrapper+0x30` 指向自有 CPU 数据，
  可见图仍跟随 `wrapper+0x08` 的 GPU resource。
- 旧 clone 显示苹果图的根因是 `clone.wrapper+0x08 == hot.wrapper+0x08`。
- clone 的 handle slot 可正确重定位为自有 wrapper；失败点不是 slot q10。
- clone descriptor blocks 继承源图时，可见图仍由源 GPU resource/view 主导。
- `+0x90/+0xE0` descriptor/view blocks 曾被验证会主导可见图像；后来进一步缩小到
  `wrapper+0x08` 的真实 GPU resource object 才是 clone 继续显示源图的根因。
- clone 的 `wrapper+0x30` CPU data 可以指向自有 buffer，但只改 CPU data 不会产生
  新的可见 GPU 资源。
- 已验证 bind 路线只填充主资源槽 `+0x88/+0x90` 时，第二套 `+0xD8/+0xE0`
  可保持为 0。

## 原生函数

已确认函数：

| 地址 | 名称 | 已确认职责 |
| --- | --- | --- |
| `0x142116B40` | `TextureDX12_bind_resource_handle_create_views` | 绑定 24-byte resource handle slot，创建 descriptor/view |
| `0x142117000` | `TextureDX12_clone_resource_handle_create_view` | 复制/alias 源 GPU resource，分配 view，但不创建自有图 |
| `0x142118A40` | `TextureDX12_create_srv_uav_descriptors` | 调 GPU/D3D 接口创建 view descriptor |
| `0x142112E30` | `TextureDX12_ctor_init_root` | 初始化 `TextureDX12` 根对象 |
| `0x142113000` | `TextureDX12_release_resource_state` | 释放两套 resource/view 状态 |
| `0x140D18D20` | `D3DResourceManager_create_resource_wrapper` | 原生 D3D resource wrapper 创建入口 |
| `0x140D19170` | `D3DResourceManager_create_placed_resource` | 原生 placed resource 创建路径 |
| `0x1420F2CF0` | `copy_resource_handle_slot_addref` | 复制 24-byte handle slot 并处理引用 |
| `0x1420F34E0` | `descriptor block allocator` | 分配 `+0x90/+0xE0` descriptor block |
| `0x1420F5FA0` | `D3DResource_set_debug_name_from_decima_string` | 给 resource 设置 debug name，不构造 handle slot |
| `0x142113810` | `TextureDX12_upload_texture_payload` | 把 reader 数据写入已绑定 GPU resource，不创建 resource |
| `0x1420C2BC0` | native resource wrapper -> bind 范例 | 展示 wrapper 创建、slot 构造、TextureDX12 bind 顺序 |

运行约束：

- `0x142116B40` 入口 hook 不稳定，已确认会破坏早期资源加载。
- 在 clone 构造完成后直接调用 `0x142116B40` 可成功返回。
- `TextureDX12_clone_resource_handle_create_view` 会通过 `0x1420F2CF0` 复制 handle，
  通过 `0x1420F34E0` 分配 descriptor block，并调用全局 GPU/D3D 接口创建 view；
  但它仍引用源 GPU resource，不能生成自有图。
- 原生 jacket upload reader 来自调用栈临时对象，已验证 reader 位于线程栈范围内；
  不适合作为长期保存或直接复用的 heap 对象。
- `TextureDX12_upload_texture_payload` 依赖 `TextureDX12+0x80/+0x88` 已有
  resource handle/wrapper；它只上传 payload，不能替代 resource 创建与 bind。

## D3D12 Resource 创建

自有 D3D12 texture resource 可通过 D3D12 API 创建。

历史验证的最小可用创建参数：

```text
source D3D12_RESOURCE_DESC
source D3D12_HEAP_PROPERTIES
D3D12_HEAP_FLAGS = 0
initial state = D3D12_RESOURCE_STATE_COMMON
```

NO DATA 源 resource 的已观察 desc：

```text
dimension=3
alignment=65536
width=512
height=320
depthArray=1
mips=1
format=99
samples=1/0
layout=0
flags=0
heapType=1
```

创建结论：

- 原样复用源 heap flags `0x44` 调 `CreateCommittedResource` 返回
  `0x80070057`。
- 将 heap flags 改为 `0` 后，`CreateCommittedResource` 成功。
- clone wrapper `+0x08` 可以指向该自有 resource。
- 之后调用 `TextureDX12_bind_resource_handle_create_views` 可让
  `TextureDX12+0x88` 保持 clone wrapper，并创建新的 `TextureDX12+0x90`
  descriptor/view block。

## D3D12 Upload

已验证上传方式：

1. 对目标 texture 调 `GetCopyableFootprints`。
2. 创建 upload heap buffer。
3. Map upload buffer 并写入合法 BC7 payload。
4. 使用 direct command queue/list 调 `CopyTextureRegion`。
5. 等待 fence。
6. copy 前 barrier: `PIXEL_SHADER_RESOURCE -> COPY_DEST`。
7. copy 后 barrier: `COPY_DEST -> PIXEL_SHADER_RESOURCE`。

20:42 已验证日志：

```text
txdx12upload prepared-bc7 width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12upload copy width=512 height=320 format=99 uploadBytes=163840 hr=0x0
txdx12own result wrapper=0x2A8B2480000 resource=0x2ADFD172770
  resourceEqOwn=1 wrapperEqClone=1
```

## BC7 格式事实

已验证当前源图格式：

```text
DXGI format = 99
DXGI_FORMAT_BC7_UNORM_SRGB
```

BC7 约束：

- 一个 BC7 block 为 16 字节。
- 一个 BC7 block 覆盖 4x4 texels。
- `512 x 320` 的 BC7 顶级 mip 数据大小为：

```text
(512 / 4) * (320 / 4) * 16 = 163840 bytes
```

内容结论：

- D3D12 resource 创建、copy、bind 全成功并不保证可见图显示。
- 20:29 版本写入任意 16-byte block，游戏内不显示自定义图。
- 20:42 版本写入合法 BC7 mode 6 solid-color block，用户确认看到测试图。
- 因此当前自定义图 payload 必须编码为合法 BC7 block 数据。

## 当前最小可行链路

```text
外部曲目 Track+0x50
  -> 自有 StreamingRef target
  -> cloned UITexture
  -> cloned Texture
  -> cloned TextureDX12 / pixelBuffer
  -> clone wrapper
  -> wrapper+0x08 = 自有 D3D12 texture resource
  -> D3D12 upload 写入合法 BC7 payload
  -> TextureDX12_bind_resource_handle_create_views 创建 descriptor/view
```

已验证结果：

- 外部曲目可以显示上传到自有 D3D12 resource 的测试图。
- 外部曲目在该链路下可稳定播放、暂停。

## 历史反证链

以下是被删交接文档中压缩后容易丢失、但仍用于理解当前实现边界的已验证事实。

### CPU pixelBuffer / DXBC 路线

- `VirtualAlloc + memcpy` 复制 HotSpring pixelBuffer，并重定位内部 qword 指针后，
  外部曲目仍可稳定播放。
- 补丁小范围 DXBC payload 和完整 first-mip payload 都没有改变可见图像。
- DXBC 页不是稳定连续图片缓冲，而是运行时会随加载状态增长、收缩、重排的
  虚拟纹理页结构。
- 已观察 DXBC 页事实：

```text
DXBC marker 起点: page+0x51
mip table 基址:  marker+0x20
mipOffs[0]:      相对 marker 的偏移，常见 0x3c
first mip 起点:  marker + mipOffs[0]，不是 16-byte alignedStart
```

因此不能把 `Texture+0x20` 后方内存当作稳定 DDS/BCn 线性缓冲来覆写。

### descriptor/resource 路线

- `UITexture+0x30` 指向 NO DATA 原生 `Texture` 时，画面变为 `NO DATA`。
- cloned `Texture+0x20` 指向 NO DATA 原生 `TextureDX12` / pixelBuffer 时，
  画面变为 `NO DATA`。
- cloned pixelBuffer 的 `+0x88/+0xD8` 可重定位到 clone 自有 wrapper，
  但 `+0x90/+0xE0` 继承 HotSpring descriptor 时仍显示苹果图。
- 进一步确认 clone wrapper `+0x08` 真实 GPU resource 继承 HotSpring 时，
  即使 wrapper `+0x30` CPU data 指向自有 buffer，画面仍显示 HotSpring。
- 手动把 clone wrapper `+0x08` 改为 NO DATA resource 后调用 native bind，
  `TextureDX12+0x88` 保持 clone wrapper，`+0x90` 创建新 descriptor，
  画面链路可跟随该 resource。

结论：已验证可行边界是创建自有 D3D12 resource、绑定到 clone wrapper、上传合法
BC7，再创建 view；不是继续扩大 CPU DXBC payload patch，也不是复用原生
upload reader。

## 已排除项

- 不是音频播放问题。
- 不是 `Track+0x50` slot 选错。
- 不是 `UITexture+0x30` 偏移错。
- 不是 `Texture+0x20` 偏移错。
- 不是 clone target / UITexture / Texture 链不能稳定工作。
- 不是单纯 CPU DXBC payload patch 可以解决的问题。
- 不是硬调 `TextureDX12_upload_texture_payload` 可以替代 resource 创建的问题。
- 不是 24-byte wrapper-backed handle slot q10 没重定位的问题。
- 不是原生 upload reader 可长期复用的问题；reader 已验证为栈上临时对象。
