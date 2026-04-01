# DS2 音乐流程知识库

本索引页只负责导航。原先的单文件知识库已经按主题拆分，后续继续按这些分文件维护。

## 入口文件

- [流程总览](./DS2MusicFlowFlows.md)
- [函数职责：播放核心](./DS2MusicFlowFunctionsPlayback.md)
- [函数职责：列表核心](./DS2MusicFlowFunctionsPlaylist.md)
- [函数职责：UI 路径](./DS2MusicFlowFunctionsUi.md)
- [状态字段与顺序样本](./DS2MusicFlowStates.md)

## 原型验证记录

- [旧方案：外部 WEM 注入原型（已证伪）](./DS2MusicFlow实现方案-外部Wem注入.md)

## 待验证专题

- [待验证记录索引](./DS2MusicFlow待验证记录.md)
- [待验证：文件定位与读链](./DS2MusicFlow待验证记录-文件定位与读链.md)
- [待验证：流媒体设备与外部文件](./DS2MusicFlow待验证记录-流媒体设备与外部文件.md)
- [待验证：历史归档](./DS2MusicFlow待验证记录-历史归档.md)

## 常用跳转

- [播放清理入口 `sub_140C12AC0`](./DS2MusicFlowFunctionsPlayback.md#sub_140c12ac0)
- [列表写入点 `sub_140C10D90`](./DS2MusicFlowFunctionsPlaylist.md#sub_140c10d90)
- [列表同步点 `sub_140C10E20`](./DS2MusicFlowFunctionsPlaylist.md#sub_140c10e20)
- [移除入口 `sub_141808AE0`](./DS2MusicFlowFunctionsUi.md#sub_141808ae0)
- [状态字段 `state30`](./DS2MusicFlowStates.md#state30)
- [状态字段 `entryState48`](./DS2MusicFlowStates.md#entrystate48)

## 维护原则

- 只记录现有日志与 IDA MCP 能稳定支持的可靠结论。
- 不记录计划、猜测、无法复现的单次现象或流水账。
- 新证据优先落到对应主题文件，不再把所有内容堆回单文件。
