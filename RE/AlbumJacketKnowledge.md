# 专辑图知识库

本文只记录 DS2MusicPlayer 自定义曲目专辑图相关的已验证知识。

## 目标

外部/自定义曲目必须能够显示自定义专辑图。当前已验证到：外部曲目可以显示
由 ASI 创建的自有 D3D12 texture resource 中的 BC7 测试图。

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

- 构造自有 target，并让 `target+0x20` 指向 cloned `UITexture`。
- 通过 `StreamingRef_UITexture` context vtable `[3]` / `assign_loaded` 把新 slot 写入
  `Track+0x50`。
- 外部曲目播放、暂停、恢复期间该链路稳定。

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

## 原生函数

已确认函数：

| 地址 | 名称 | 已确认职责 |
| --- | --- | --- |
| `0x142116B40` | `TextureDX12_bind_resource_handle_create_views` | 绑定 24-byte resource handle slot，创建 descriptor/view |
| `0x142117000` | `TextureDX12_clone_resource_handle_create_view` | 复制/alias 源 GPU resource，不创建自有图 |
| `0x140D18D20` | `D3DResourceManager_create_resource_wrapper` | 原生 D3D resource wrapper 创建入口 |
| `0x142113810` | `TextureDX12_upload_texture_payload` | 把 reader 数据写入已绑定 GPU resource，不创建 resource |
| `0x1420C2BC0` | native resource wrapper -> bind 范例 | 展示 wrapper 创建、slot 构造、TextureDX12 bind 顺序 |

运行约束：

- `0x142116B40` 入口 hook 不稳定，已确认会破坏早期资源加载。
- 在 clone 构造完成后直接调用 `0x142116B40` 可成功返回。

## D3D12 Resource 创建

自有 D3D12 texture resource 可通过 D3D12 API 创建。

已验证最小可用创建参数：

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
3. Map upload buffer 并写入 BC7 payload。
4. 使用 direct command queue/list 调 `CopyTextureRegion`。
5. 等待 fence。
6. 将目标 resource 转到 `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`。
7. 调 `TextureDX12_bind_resource_handle_create_views` 创建 view。

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

## 代码入口

当前专辑图相关核心文件：

- `ds2_music_player_asi/CustomJacketUiClone.cpp`
- `ds2_music_player_asi/CustomJacketPixelBufferClone.cpp`
- `ds2_music_player_asi/CustomJacketD3D12Create.cpp`
- `ds2_music_player_asi/CustomJacketD3D12Upload.cpp`
- `ds2_music_player_asi/CustomJacketD3D12Resource.cpp`
- `ds2_music_player_asi/CustomJacketTextureDx12Bind.cpp`
- `ds2_music_player_asi/TextureUploadProbe.cpp`
- `ds2_music_player_asi/TextureUploadHistory.cpp`

## 已排除项

- 不是音频播放问题。
- 不是 `Track+0x50` slot 选错。
- 不是 `UITexture+0x30` 偏移错。
- 不是 `Texture+0x20` 偏移错。
- 不是 clone target / UITexture / Texture 链不能稳定工作。
- 不是单纯 CPU DXBC payload patch 可以解决的问题。
- 不是硬调 `TextureDX12_upload_texture_payload` 可以替代 resource 创建的问题。
