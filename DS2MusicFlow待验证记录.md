# DS2 音乐流程待验证记录

本页只做导航，不再继续堆单一流水账。

这些内容的定位是：
- 已经有静态证据或局部硬证据支撑。
- 但还没有完成运行时闭环验证。
- 因此暂时不能直接写入主知识文件。

## 当前分文件

- [文件定位与读链](./DS2MusicFlow待验证记录-文件定位与读链.md)
- [流媒体设备与外部文件](./DS2MusicFlow待验证记录-流媒体设备与外部文件.md)
- [Wwise External Source 与 SetMedia](./DS2MusicFlow待验证记录-WwiseExternalSource与SetMedia.md)
- [历史归档（旧单文件）](./DS2MusicFlow待验证记录-历史归档.md)

## 原型验证记录

- [旧方案：外部 WEM 注入原型（已证伪）](./DS2MusicFlow实现方案-外部Wem注入.md)

## 当前共识

- 已证实音乐 low-level 读链会构造固定键 `cache:streams/%x/%x/%x/%x/%s.%02x.stream`。
- 已证实启动阶段会加载外部文件 `LocalCacheWinGame/package/streaming_graph.core`。
- 已证实 `StreamingGraphResource + 0x150` 字段名为 `Files`，并且会被传给读设备去打开文件列表。
- 已证实底层设备具备“逻辑键解析后走 `CreateFileW` 打开实际文件”的能力。
- 已证实 `DecompressingReadDevice` 会消费 `streaming_graph.core` 提供的映射表，把高层请求翻译成底层包文件偏移读取与解压。
- 已证实包装层真正拿来查 `streaming_graph` 表的字段是请求对象 `+0x08`，而音乐链当前静态构造该字段为 `-1`。
- 已证实读请求对象本身明确带有 `buffer / offset / size / completion callback` 字段，因此“按原协议喂外部 WEM 字节”在结构上是可落地的，问题只剩识别外部请求与正确回调。
- 当前测试版已按“高层选外部 WEM，低层完成后按 `offset / size` 覆写缓冲”的方案实现：
  - `sub_140C12580 / sub_140C15560` 负责选中外部文件
  - `sub_142073210 / sub_1420734A0` 负责捕获并覆写读缓冲
- 已证实 `sub_1426932B0` 的请求参数里，`request+0x08` 就是 `fileId`，并且在名称为空时会直接格式化 `L"%u.wem"`。
- 已修正一条旧误判：
  - 目前没有 IDA 里的明确调用链，能把 `sub_140C12580 / sub_140C15560` 或 `externalMusic armed` 直接连到 `sub_1426932B0 / sub_142693510`
  - 因此，`sub_1426932B0 / sub_142693510` 当前只能写成 resolver 观察点，不能再写成“已经证实属于实际播放主链的节点”
- 已证实当前存在一条完全不依赖 resolver 的 Wwise 提交链：
  - `sub_140C12580 / sub_140C15560`
  - `sub_140AC5210`
  - `sub_142684A30`
  - `sub_1426948C0`
  - `WwiseSimpleSoundInstance` 状态方法
  - `sub_142695420`
  - `sub_1426B00A0`
  - `AK::SoundEngine::PostEvent(..., AkExternalSourceInfo *, count)`
- 已证实游戏自带 `AK::SoundEngine::SetMedia / TryUnsetMedia / UnsetMedia`，且 `SetMedia` 的输入数组项至少按 `0x18` 步长包含：
  - `32 位键值`
  - `媒体内存指针`
  - `媒体字节长度`
- 因而，当前已经有一条新的 IDA 候选方案：
  - 不再尝试把目录外部 `.wem` 伪装成包内 `fileId`
  - 而是在 `sub_142695420 / sub_1426B00A0` 这一层，先把外部 `.wem` 作为内存媒体注册给 Wwise，再继续原始 `PostEvent`
- 当前原型实现也已经切到这条新路线，但还没有运行时验证结果。
- 已证实 `sub_142695420` 曾因签名定义中的单字节错误而扫描失败：
  - 工程旧签名写成 `44 8B E9`
  - IDA 实际函数头字节是 `41 8B E9`
  - 修正后，IDA `find_bytes` 能唯一命中 `0x142695420`
- 已证实 `sub_14206A1B0` 的第二个参数是 16 字节 key、第三个参数是流索引字节，返回内容就是构造出的 `cache:streams/...` 字符串对象。
- 已证实旧原型里，高层播放/试听入口已经能选中外部文件，并产生日志 `externalMusic armed`。
- 已证实旧原型运行样本里没有出现 `hit sub_1426E7CC0` 或 `externalMusic override hit`，并且实机听感仍为原版音乐。
- 因而，`sub_1426E7CC0` 作为“当前可用的外部 WEM 覆写注入点”已经被运行时证伪。
- 仍未证实当前音乐请求里的 `cache:streams/...` 是如何命中 `Files` 里的条目。
- 仍未证实“把用户目录里的任意 `.wem` 直接接进当前音乐链”已经天然可行。

## 维护规则

- 新证据优先写入对应专题页。
- 只有跨专题的阶段性结论，才回写到本页。
- 旧的大体量流水记录保留在历史归档，不再继续追加。
