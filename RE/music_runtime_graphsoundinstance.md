# 音乐运行时与 GraphSoundInstance 整理

## 证据分级

- 静态候选：只由 IDA 静态分析支持，尚未被当前目标流程命中
- 运行已命中：已被当前目标流程命中，但尚未证明是最终替换边界
- 已验证边界：既被当前目标流程命中，又能稳定关联到目标资源与实际字节流处理

当前结论里没有“已验证边界”。

## 运行已命中：全局对象与主实例

- `qword_14622EDA8` 为音乐运行时全局指针，运行时可作为 `MusicRuntime *` 使用
- `MusicRuntime + 0x1918` 为 `currentPlayerObj`，运行时 RTTI 已命中为 `GraphSoundInstance *`
- `MusicRuntime + 0x2830` 为 `secondaryPlayerObj`
- `MusicTrackObject + 0x40` 为 `primaryDesc`，运行时 RTTI 已命中为 `GraphSoundResource *`

## 运行已命中：实例创建路径

当前目标流程里，已实际命中过以下链路：

```text
DSUIMusicMenu_HandlePlayNextMusic
-> MusicRuntime_AdvanceNextTrack
-> MusicRuntime_ApplySelectedEntry
-> AcquireRuntimeObjectAndEnableOnStart
-> sub_7FF649DB9710
-> GraphSoundResource::createInstance
```

其中：

- `AcquireRuntimeObjectAndEnableOnStart` 入口的 `RCX` 运行时 RTTI 为 `GraphSoundResource`
- `sub_7FF649DB9710` 调用 `a3->vftable[4]`
- 该虚表槽运行时真实目标为 `sub_7FF649DDAD80`
- `sub_7FF649DDAD80` 分配 `0x340` 字节对象，并写入 `GraphSoundInstance::vftable`

这说明：

- 音乐描述对象不是直接播放，而是先实例化成 `GraphSoundInstance`
- `GraphSoundResource::createInstance` 是主音乐实例构造路径上的已命中边界

## 运行已命中：GraphSoundInstance 关键方法

### `vftable + 0xF8`

运行时真实函数：

```text
sub_7FF69ED152E0
```

行为：

- 调 `AK::SoundEngine::RegisterGameObj(self)`
- 调 `AK::SoundEngine::SetPosition(self, ...)`
- 直接消费实例内的位置与朝向字段

已命中字段：

- `+0x20/+0x28/+0x30`：位置
- `+0x44/+0x48/+0x4C`：forward
- `+0x50/+0x54/+0x58`：up
- `+0x5C`：状态位字段
- `+0x186`：状态位字段

### `vftable + 0x110`

运行时真实函数：

```text
sub_7FF69ED157C0
```

行为：

- 只在 `soundInterface328` 非空时继续
- 根据入参切换 `controlBlock320` 内的字节位
- 读写 `stateByte180`
- 清理 `flags060` 的 `0x100`

已命中字段：

- `+0x60`：`flags060`
- `+0x180`：`stateByte180`
- `+0x320`：`GraphSoundControlBlock *`
- `+0x328`：`GraphSoundInterface *`

### `vftable + 0x118`

运行时真实函数：

```text
sub_7FF69ED15820
```

行为：

- 命中于退出到标题界面
- 若 `controlBlock320` 非空，则切换 `controlBlock320->byte13`
- 把入参写入 `controlBlock320->dword100`
- 读写 `stateByte180` 的 bit0

该点已命中，但当前只证明是退出/切换状态点，不是常规播放主路径。

## 运行已命中：GraphSoundInstance 结构

### 主链字段

```cpp
struct GraphSoundInstance {
    GraphSoundInstanceVftable *vftable;
    double posX;                       // +0x20
    double posY;                       // +0x28
    double posZ;                       // +0x30
    float forwardX;                    // +0x44
    float forwardY;                    // +0x48
    float forwardZ;                    // +0x4C
    float upX;                         // +0x50
    float upY;                         // +0x54
    float upZ;                         // +0x58
    uint32_t stateFlags5C;             // +0x5C
    uint16_t flags060;                 // +0x60
    void *unk170;                      // +0x170
    GraphSoundBindingRef *binding178;  // +0x178
    uint8_t stateByte180;              // +0x180
    uint16_t postedSlotCount182;       // +0x182
    uint16_t stateWord184;             // +0x184
    uint16_t stateFlags186;            // +0x186
    SRWLOCK lock248;                   // +0x248
    int32_t playingSlotTableCount;     // +0x250
    GraphSoundPlayingSlot *playingSlots258; // +0x258
    uint16_t activePlayingSlotCount;   // +0x260
    uint8_t counter2BA;                // +0x2BA
    GraphSoundControlBlock *controlBlock320; // +0x320
    GraphSoundInterface *soundInterface328;  // +0x328
};
```

### 资源绑定链

主音乐实例上，当前稳定可用的资源链是：

```text
GraphSoundInstance::binding178
-> GraphSoundBindingRef::binding
-> GraphSoundBinding::resource
-> GraphSoundResource
```

反证：

- `GraphSoundInstance + 0x170` 不是主资源字段
- 主音乐实例上该字段可为 `0`
- 真正稳定命中的资源链在 `+0x178`

## 运行已命中：GraphSoundControlBlock

```cpp
struct GraphSoundControlBlock {
    uint64_t unk00;
    uint64_t unk08;
    uint8_t unk10;
    uint8_t byte11;
    uint8_t byte12;
    uint8_t byte13;
    uint8_t unk14[0xEC];
    uint32_t dword100;
};
```

已命中用途：

- `byte11`：`sub_7FF69ED157C0(a2 != 1)` 会置位
- `byte12`：`sub_7FF69ED157C0(a2 == 1)` 会置位
- `byte13`：`sub_7FF69ED15820(a2 != 0)` 会置 `1`，否则清 `0`
- `dword100`：`sub_7FF69ED15820` 会写入第三个参数

## 运行已命中：GraphSoundPlayingSlot

```cpp
struct GraphSoundPlayingSlot {
    uint32_t playingId;
    uint32_t eventId;
    uint16_t slotFlags;
    uint16_t useCount;
    uint8_t lookupKey0C;
    uint8_t callbackCookie0D;
};
```

已命中用途：

- `playingId`：`AK::SoundEngine::PostEvent` 成功返回后写回
- `eventId`：与本次 `PostEvent` 的事件 ID 一起写回
- `slotFlags`
  - 新槽 append 后先置 `0x10`
  - 成功拿到 `playingId` 后变为 `0x90`
- `useCount`：成功写回后自增
- `lookupKey0C`：用于在槽表中查找/复用同类槽位
- `callbackCookie0D`：作为 `PostEvent` 的 callback cookie 参数传入

## 运行已命中：PostEvent 提交路径

### 已命中边界

```text
sub_7FF69ED24EE0
-> sub_7FF69ED34D80
-> AK::SoundEngine::PostEvent
```

`sub_7FF69ED34D80` 已在 `currentPlayerObj` 上运行命中，且当前流程里能直接看到：

- 新槽位 append
- `playingId` 回写
- `eventId` 回写
- `useCount` 增加
- `postedSlotCount182` 增加

### 主音乐实例首发路径

当前目标流程里，主音乐实例出现过这组前态：

```cpp
self->playingSlotTableCount   = 1;
self->activePlayingSlotCount  = 0;
self->postedSlotCount182      = 0;
self->stateFlags186           = 0x0406;
slot[0] = {0};
```

然后走：

```text
sub_7FF69C8E72D0 返回新槽下标 0
-> ++activePlayingSlotCount
-> slot[0].slotFlags |= 0x10
-> slot[0].playingId == 0，跳过旧句柄清理
-> 直接发起新的 PostEvent
```

已命中的一次主音乐实例 `PostEvent` 形状：

```cpp
eventId        = 0x68559DFC;
self           = currentPlayerObj;
flags          = 0x00100001;
callback       = sub_7FF69ED33F10;
cookie         = slot->callbackCookie0D;
externalCount  = 0;
externalInfo   = 0;
```

该次返回：

```cpp
playingId = 0x552;
```

随后进入成功写回：

```cpp
slot->playingId = 0x552;
slot->eventId   = 0x68559DFC;
++slot->useCount;
slot->slotFlags = 0x90;
self->stateFlags186 = 0x0C96;
++self->postedSlotCount182;
```

## 运行已命中：停止/清理路径

```text
sub_7FF69ED34360
```

该函数已命中并直接消费：

- `+0x182`
- `+0x184`
- `+0x186`
- `+0x248`
- `+0x250`
- `+0x258`
- `+0x260`

已命中行为：

- 遍历当前活动 `playingId`
- 调 `AK::SoundEngine::CancelEventCallback`
- 必要时调 `AK::SoundEngine::ExecuteActionOnPlayingID`
- 清空槽表中的 `playingId`
- 清 `slotFlags` 的相关位
- 把 `useCount` 清零
- 把 `activePlayingSlotCount` 清零

其中：

- `+0x184` 会被该函数自旋等待归零
- 但当前会话里尚未抓到谁负责写 `+0x184`

## 运行已命中：GraphSoundResource

```cpp
struct GraphSoundResource {
    GraphSoundResourceVftable *vftable;
    volatile int32_t refCount; // +0x08
    int32_t resourceKind;      // +0x0C
    uint64_t keyLo;            // +0x10
    uint64_t keyHi;            // +0x18
};
```

已命中用途：

- `refCount`：被原子加减
- `resourceKind`：被逻辑分支直接读取
- `keyLo/keyHi`：作为资源身份键参与查找/绑定

## 新增反证

- `sub_7FF69ED33F10` 在当前目标流程里，针对 `currentPlayerObj` 的条件断点零命中
- 因此不能把 `sub_7FF69ED33F10(a1 == 0x2000, currentPlayerObj)` 当成主音乐实例当前流程的既定回调路径
- `GraphSoundInstance::vftable + 0x118` 只在退出到标题界面时命中，当前不能把它当作常规播放主路径
- `GraphSoundInstance + 0x170` 不是主资源字段

## 当前最有价值但未收口的点

- `GraphSoundInstance + 0x184` 的真实写入者
- `GraphSoundPlayingSlot::lookupKey0C` 对应的实际分组语义
- `sub_7FF69ED34D80` 之后是否还存在更靠近字节流提交的音乐专用边界
