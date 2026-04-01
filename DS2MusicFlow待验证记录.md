# DS2 音乐流程待验证记录

本页只做导航，不再继续堆单一流水账。

这些内容的定位是：
- 已经有静态证据或局部硬证据支撑。
- 但还没有完成运行时闭环验证。
- 因此暂时不能直接写入主知识文件。

## 当前分文件

- [文件定位与读链](./DS2MusicFlow待验证记录-文件定位与读链.md)
- [流媒体设备与外部文件](./DS2MusicFlow待验证记录-流媒体设备与外部文件.md)
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
- 已证实旧原型里，高层播放/试听入口已经能选中外部文件，并产生日志 `externalMusic armed`。
- 已证实旧原型运行样本里没有出现 `hit sub_1426E7CC0` 或 `externalMusic override hit`，并且实机听感仍为原版音乐。
- 因而，`sub_1426E7CC0` 作为“当前可用的外部 WEM 覆写注入点”已经被运行时证伪。
- 仍未证实当前音乐请求里的 `cache:streams/...` 是如何命中 `Files` 里的条目。
- 仍未证实“把用户目录里的任意 `.wem` 直接接进当前音乐链”已经天然可行。

## 维护规则

- 新证据优先写入对应专题页。
- 只有跨专题的阶段性结论，才回写到本页。
- 旧的大体量流水记录保留在历史归档，不再继续追加。
