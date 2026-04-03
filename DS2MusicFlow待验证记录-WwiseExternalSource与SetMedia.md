# DS2 音乐流程待验证记录：Wwise External Source 与 SetMedia

本页只记录一条完全基于 IDA MCP 静态分析的新候选方案：
不再走 resolver / `%u.wem` / `fileId -> 外部路径` 的旧思路，而是直接利用 Wwise 已经暴露出来的 external source 与内存媒体接口。

## 已确认的高层到 Wwise 提交链

- `sub_140C12580` 与 `sub_140C15560` 都会明确调用 `sub_140AC5210`。
- `sub_140AC5210` 明确调用 `sub_142684A30(qword_14A10C580, 0, a1, 0)`。
- `sub_142684A30` 会取资源对象虚表 `+0x20`。
- `WwiseSimpleSoundResource` 与 `WwiseWemSoundResource` 的该槽位都会落到 `sub_1426948C0`。
- `sub_1426948C0` 会构造 `WwiseSimpleSoundInstance`，并把实例挂到 `WwiseSimpleSoundInstance::vftable`。
- `WwiseSimpleSoundInstance` 上至少有以下状态方法会汇到 `sub_142695420`：
  - `sub_142695580`
  - `sub_142695690`
  - `sub_1426956F0`
  - `sub_142695750`
  - `sub_142695990`

## 已确认的 external source 提交点

- `sub_142695420` 会在栈上构造一个单项描述结构，然后调用 `sub_1426B00A0`。
- 从 `sub_142695420` 的汇编可直接读出这项结构的写法：
  - `+0x00` 写入一个 32 位键值，来源于资源相关对象的 `+0xD8`
  - `+0x04` 固定写入 `4`
  - `+0x08` 到 `+0x17` 清零
  - `+0x18` 写入另一个 32 位值，来源于返回对象的 `+0x20`
- `sub_142695420` 传给 `sub_1426B00A0` 的最后两个关键参数是：
  - `a13 = 1`
  - `a14 = &localDescriptor`
- `sub_1426B00A0` 里已明确存在对
  `AK::SoundEngine::PostEvent(uint, uint64, uint, callback, cookie, uint, AkExternalSourceInfo*, uint)`
  的直接调用。
- 因而可以静态确认：
  当前音乐实例不是直接把 `%u.wem` 交给 Wwise，而是会先组一个 1 项 external source 描述，再交给 `PostEvent`。

## 已确认的 SetMedia / UnsetMedia 接口

- 二进制里有真实函数：
  - `AK::SoundEngine::SetMedia`
  - `AK::SoundEngine::TryUnsetMedia`
  - `AK::SoundEngine::UnsetMedia`
- `AK::SoundEngine::SetMedia` 的内部实现是 `sub_1428A4660`。
- `sub_1428A4660` 明确按 `0x18` 步长遍历输入数组，每项至少会读取：
  - `+0x00` 的 32 位键值
  - `+0x08` 的 64 位媒体指针
  - `+0x10` 的 32 位媒体长度
- `sub_1428A4660` 会把这些项挂到一个按 32 位键值索引的内部表上。
- `sub_1428A9D40` 与 `sub_1428AA7C0` 分别支撑 `UnsetMedia / TryUnsetMedia`，它们同样按 `0x18` 步长遍历，并用每项的 `+0x00` 作为查找键。
- 因而可以静态确认：
  Wwise 侧已经具备“按 32 位键值注册一段内存媒体数据”和“按同一键值卸载媒体”的现成接口。

## 基于 IDA 的可行实施方案

- 高层 hook 只负责决定当前播放/试听要替换成哪个外部 `.wem`。
- 真正的数据接入点放在 `sub_142695420` 或 `sub_1426B00A0`，不再依赖 resolver 支线。
- 命中替换条件时：
  - 读取 `sub_142695420` 当前即将提交给 Wwise 的那项 32 位键值
  - 从目录读取外部 `.wem` 全文件到内存
  - 按 `sub_1428A4660` 已证实的 `0x18` 布局构造 1 项 `SetMedia` 输入
  - 调用游戏自带的 `AK::SoundEngine::SetMedia`
  - 再继续走原始 `sub_1426B00A0 -> PostEvent`
- 切歌、停止或替换下一首时：
  - 用同一个 32 位键值调用 `UnsetMedia` 或 `TryUnsetMedia`
  - 再释放我们自己持有的外部 `.wem` 缓冲

## 这条方案为什么比旧线更贴近目标

- 它不要求先证明 `%u.wem`、`cache:streams/...`、`streaming_graph.core` 和播放器当前请求之间的完整运行时闭环。
- 它直接利用了 Wwise 已经存在的“external source 提交”与“内存媒体注册”接口。
- 对“让游戏内播放器播放目录里的额外音乐”这个目标来说，这条路线的核心动作是：
  - 我们自己读目录里的外部 `.wem`
  - 我们自己把字节注册给 Wwise
  - 游戏原本的 `PostEvent` 继续照常触发

## 当前仍待运行时验证的唯一关键点

- `sub_142695420` 里那项 32 位键值，是否正是 `SetMedia / UnsetMedia` 需要使用的同一把键。
- 这是当前方案里唯一还需要实机闭环验证的地方。
- 但从 IDA 角度看，这已经是目前最短、最直接、且最符合“额外音乐目录接入”目标的一条实施路线。

## 当前原型实现状态

- 当前工程已经把原型切到这条新路线：
  - 高层仍由 `sub_140C12580 / sub_140C15560` 选择外部 `.wem`
  - `sub_1426B00A0` detour 在 `PostEvent` 前尝试绑定外部 media
  - 绑定动作会先调用游戏自带 `AK::SoundEngine::SetMedia`
  - 绑定成功后，会把传给 `PostEvent` 的 external source 描述改成内存媒体形态
- 这仍然只是“待验证原型”，还不能写成已证实结论。

## 2026-04-03：`sub_142695420` 签名修正

- 运行时未出现 `sub_142695420 originalExternalSource` 时，先检查了 optional hook 的初始化日志，确认问题不是 detour 逻辑，而是 `sub_142695420` 签名扫描失败。
- 用 IDA 直接读取 `sub_142695420` 函数头机器码后，确认工程中的旧签名有一个字节写错：
  - 工程旧值：`44 8B E9`
  - IDA 实际字节：`41 8B E9`
- 将该字节修正后，再用 IDA `find_bytes` 验证，修正后的签名在 IDB 中唯一命中 `0x142695420`。
- 因而，这一轮得到的结论是：
  - `sub_142695420` 之前扫不到，不是函数本身不稳定，也不是模式长度过长
  - 根因就是签名定义里存在单字节错误
