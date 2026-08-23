# 快速下车：ActionGraph 长结果与根运动时间区间

日期：2026-08-23

返回[当前状态与知识索引](FastVehicleBoardingModImplementation.md)，或继续阅读
[根运动与安全落点](FastVehicleRideOffRootMotionAndLanding.md)。

> **当前结论：**把长 descriptor 一次强制到严格末端会留下玩家动作冻结，不能作为
> 快速下车实现。已经通过运行验证的实现改为每次最多额外推进 `0.25s`，同时把同一份
> 额外时间加入实际 RideOff 动态表的 post-evaluate 时钟，并在 descriptor 终点前
> `0.1s` 停止注入。最后阶段由原生生命周期完成；卡车驾驶位样本在约 `1.094s` 原生
> 退出 RideOff、同帧进入 Basic，后续移动输入有效且没有进入 Fall。

## 长 descriptor 与 fullgame 结果传播

此前 `RideOffDescriptorTrace` 只在 RideOff Update 的线程局部范围内观察 fullgame
evaluator；快速终结器约 16ms 后退出该范围，因此旧日志只看到若干
`duration=0.0166833` 的短结果，不能代表退出后仍在活动的图结果。只读诊断样本
`dismount_20260726_094026_586` 把观察范围扩展到同一 5 秒会话，并只记录
`duration>0.05s` 的结果后，确认同一 descriptor 在 RideOff 已请求 Free 后仍持续出现：

```text
callerRva=0x366F423
mode=0 scale=1 pose=1
duration=2.1021
sync=0.00834168, 0.0166834, 0.0291959, ... 0.1001
end=0
```

该 descriptor 地址在本轮中保持为 `0x21EC6BD7A90`，同步时间按原生帧持续前进；
`2.1021s` 也与本轮下车 CutIn 的原生 duration 相同，并接近精确截图中约 2 秒的玩家
动作持续时间。由此已确认旧 TLS 观察窗确实漏掉了一个跨 RideOff 状态退出继续推进的
长 fullgame 结果，并把后续静态分析入口收窄到返回点 RVA `0x366F423`。仅凭时长相关性
还不能证明该结果独占玩家 root motion，因而本轮没有对它改速或写入完成状态。

### 2026-07-26 `fullgame+0x366F423` 定点静态闭合

对当前 `fullgame.dll` 的调用点及结果传播链进行定点反汇编复核后，确认日志中的
`callerRva=0x366F423` 是 `0x366F41D` 间接调用后的返回点。调用只在
`work+0x22` 或 `work+0x13` 非零时发生，五个实参为：

```text
output       = work+0x1220
descriptor   = *(catalog+0x2A00)
mode         = 0
timeScale    = 1.0f
evaluatePose = 1
```

`timeScale` 所读常量的原始字节为 `00 00 80 3F`。调用返回后代码立即覆盖 `EAX`，
没有消费 evaluator 的寄存器返回值；结果由 `work+0x1220` 输出槽及其引用计数结构
向下游传播。该点直接读取固定的 `catalog+0x2A00` descriptor 槽，局部没有 hash
查找或状态 tag 比较，因此 descriptor 的语义解析发生在更上游。

该结果槽以 index `2` 进入两组对称的三路选择，分别生成 `work+0x2A0` 和
`work+0x1100`；两者又经过由原函数 `XMM2` 控制的十路选择生成 `work+0x3C0` 和
`work+0xD90`。随后 `DWORD work+0x48` 被钳制并舍入为 `0/1`：

```text
0 -> work+0xD90
1 -> work+0x3C0
       |
       v
   work+0x120
```

`work+0x120` 最后作为 index `1` 进入由原函数 `XMM1` 控制的三路选择，并写入原函数
第五参数指定的结果槽。这里确认的是一个跨多级选择器继续存活的共享动画结果节点；
局部代码没有玩家实体检查、RideOff/CutIn/Drive 标记、左右方向 tag 或世界变换写入，
所以不能把该返回点排他命名为“玩家 RideOff”，也不能确定 `work+0x48` 的 `0/1`
分别对应哪一侧。该静态闭合进一步证明时长相关性本身不能提供安全落地点，也不构成
恢复已证伪 descriptor `scale=512` 方案的依据。

对这个间接 evaluator 及其结果结构的进一步定点复核还关闭了“从长结果直接取得动作
末端”的可能性。`0x366F41D` 调用的指针槽位于 fullgame
`RVA 0x84D1FF8` 的 `.data`，文件初值为零；PE 的 Import、IAT 和 Delay Import
目录均为空，该 RVA 也不在重定位表或导出表中。因此它是运行时填充的调度槽，仅凭
fullgame 文件不能恢复实现地址或模块归属。

fullgame IDA 中对该槽的唯一写入进一步确认了初始化协议：
`CoreLibraryInitializer` 在 `0x18001A731` 把哈希 `0x63A317E9` 交给宿主传入的
解析回调，并在 `0x18001A738` 把返回地址保存到
`ActionGraph_EvaluateDescriptorToResult_Ptr`。因此 evaluator 是宿主/core API，
不是 fullgame 内部函数；fullgame 只能确定解析哈希、调用 ABI 和结果传播，不能对
实际实现继续反编译。对应 IDA 指令已添加注释。

结果传播的 mask `0x40` 也没有在 fullgame 内解码空间数据。
`ActionGraphResult_MergeChannelsByMask` 只把完整的 output/input result 指针交给
另一个宿主槽；`CoreLibraryInitializer` 在 `0x18001ADB1` 以哈希
`0x585C1E07` 解析该槽。该槽已命名为
`ActionGraphResult_MergePayloadChannel_Ptr`。所以 fullgame 的可见边界只是委托
宿主合并不透明 payload，没有读取 translation、rotation 或矩阵；payload 指针内部
是否包含更深层资源属于宿主实现边界，不能从当前 fullgame caller 直接取作落地点。
对应解析点和调用点均已添加 IDA 注释。

该调用的五个显式参数中只有输出槽、descriptor、mode、timeScale 和一个控制字节，
没有独立的绝对时间、归一化时间、目标 sync、duration、末端采样标志或
root-transform 输出地址。DS2 宿主实现进一步证明，采样区间不是额外函数参数，而是
由 `outputResult+0x08` 指向的时间状态对象携带：该对象 `+0x10` 与 `+0x18` 的两个
double 字段分别提供区间端点。fullgame 调用点传入的是已有工作结果，因此它依赖结果
对象中预先建立的时间状态，并未在调用现场直接指定时刻。

返回后 caller 不使用 `RAX`，也不从 `work+0x1220` 读取向量、四元数或矩阵；它只
传播结果中的引用与状态，再交给多级选择器。结果成员操作代码进一步确认：

- `result+0x50` 是同步区间重基准覆盖值，不是空间位移分量；
- `result+0x54` 是该标量的有效/门控状态；
- `result+0x58` 是引用传播计数；
- `result+0x08` 指向时间/同步状态对象；
- `result+0x48..0x4F` 和 `result+0x56` 目前只有清零、复制证据，不能命名为空间变换；
- 运行日志中的 `pose=1` 是 evaluator 的附加通道控制参数，不是当前骨骼姿态或某个
  root transform 数值；其 DS2 宿主实现语义见下节。

所以 `duration=2.1021` 和逐帧增长的 sync 只能说明当前结果的时间进度，不能给出下车
方向、距离或最终世界变换。evaluator 本身可以消费结果对象携带的时间区间，但当前
fullgame 调用边界只复用既有结果状态，没有建立独立任意时刻区间、提取最终 motion
transform 或把它转换为 RideOff 世界落点的接口。因此当前 caller 仍不能直接提供
RideOff 的末端 root motion，也不能用这个长结果计算提交前绕过所缺失的安全车外落点。

### 2026-07-26 DS2 宿主 evaluator 与根关节显式采样边界

切回 DS2 数据库后，fullgame 哈希 `0x63A317E9` 对应的宿主实现已经按直接地址闭合：

- `0x14219D390` 已命名为
  `ActionGraph_EvaluateDescriptorToResult_Thunk`；它只把第 5 个栈参数规范化为 byte，
  然后跳转核心；
- `0x14219A7F0` 已命名为 `ActionGraph_EvaluateDescriptorToResult`，其 ABI 确认为
  `(outputResult, descriptor, mode, timeScale, evaluateExtraChannels)`；
- 核心从 `outputResult+0x08` 取得时间状态对象；该对象 `+0x10` 是 double 区间
  终点，`+0x18` 是 double 区间起点。当时间状态 `+0x0C` 非零时，两个端点还会通过
  `ActionGraphTimeMap_MapSegmentPhaseToSeconds` 做分段时间映射，随后才乘以
  `timeScale`；
- `mode=0` 会把区间端点钳制到 descriptor duration；非零 mode 则执行跨周期处理；
- 核心读取 `descriptor+0x3C` duration，并把 `duration / timeScale` 写到
  `outputResult+0x48`，与 fullgame 调用边界完全一致。

第 5 参数非零时，核心在 `0x14219AD38` 以上述区间起点和 `jointIndex=0` 调用
`AnimationDescriptor_SampleJointTransformAtTime`（`0x142684B60`）。后者把时间钳制到
`0..min(descriptor duration, EdgeAnim animation duration)`，再调用
`EdgeAnim_SampleJointAtTime`（`0x142740450`）。该底层函数的原生断言字符串直接标出
`outputJoint`、`anim` 和 `skel`，并校验 animation/skeleton tag；其第 4 参数是
16 位 joint index，第 5 个栈参数是秒数时间。输出的 quaternion、translation、scale
随后由 `TransformMatrix_FromQuaternionTranslationScale` 转为 64 字节矩阵。

另一个独立调用者 `0x140314B30` 会先通过原生 `AnimationPlayback_SetTime` 按 end mode
循环或钳制目标时间，再以 `jointIndex=0` 调用同一 sampler，把矩阵缓存到
`playback+0x30`。这交叉确认了 `0x142684B60` 的语义是“按显式秒数采样指定关节
transform”，而不是推进 Graph 或姿态管线。

ActionGraph 核心没有把这个根关节矩阵原样作为最终结果返回。它还会提取已经求值的姿态
根变换，对显式采样矩阵求仿射逆并进行矩阵组合；循环跨越时还会重复组合 descriptor
中的整周期变换。最终在 `0x14219AEB9` 把组合矩阵的平移写到当前结果 item `+0x10`，
并在 `0x14219AF59` 把旋转转为 quaternion 写到 item `+0x00`。因此，fullgame
看不到的 mask `0x40` payload 在 DS2 宿主内已经确认包含 motion transform。

这里同时限定了结论：五参数 ABI 没有显式时间参数，但 evaluator 会间接消费
`outputResult` 已持有的时间状态；两者不能混为“完全不能指定时间”。另一方面，
`AnimationDescriptor_SampleJointTransformAtTime(descriptor, duration, 0, ...)` 得到的
仍只是根关节采样矩阵；原生 motion payload 还依赖已求值姿态、矩阵相对关系和循环
累计。因此“单独采样 duration 就等于玩家安全车外世界落点”仍不成立，不能据此直接
写玩家坐标。标准 scratch 结果的实际构造与上下文边界见下节。

`ActionGraphTimeMap_MapSegmentPhaseToSeconds`（`0x14018BE00`）的汇编进一步确认了
映射布局：`timeMap+0x00` 是 segment 数量，`+0x08` 指向逐 segment 的 float 时长
数组，`+0x10` 是最终比例。输入 phase 的整数部分经 segment 数量除法拆成完整周期与
当前 segment，函数累计此前 segment 时长，再用 phase 小数部分插值当前 segment，
最后乘以比例并返回秒数。它是分段相位到秒数的映射器，不是 descriptor duration
getter；函数、原型与关键除法/插值点已在 IDA 中更新。

`ActionGraphResult_GetTimeIntervalLengthSeconds`（`0x140207740`）读取同一时间状态，
必要时先映射两个 phase，再明确返回 `end(+0x10) - start(+0x18)`。直接 evaluator
调用者 `0x1421A8420` 的更新块也交叉确认了字段方向：它先保存旧区间长度，将新
segment phase 映射并取模后写为新终点，再把
`max(newEnd - oldIntervalLength, 0)` 写为新起点；`+0x00/+0x04` 是对应的 float
镜像。随后 `0x1421A85C5` 才用这段已写入结果对象的区间调用 evaluator。相关函数名、
原型和读写点注释均已更新到 IDA。

### 2026-07-26 标准 scratch 结果只在活动 Graph 求值上下文内使用

另一个直接调用者 `0x1421A7BC0` 已命名为
`ActionGraph_SelectBestDescriptorByPoseError`。它遍历 descriptor 候选，在指定时刻
求值各自姿态，并按选定关节的 quaternion/translation 误差返回最优候选。其 scratch
结果构造给出了明确的所有权边界：

- 函数首先在 TLS 的活动 AnimationGraph evaluation-context 注册表中查找当前实例；
  没有匹配上下文时直接返回，不会调用 evaluator；
- 它从活动 Graph 对象复制一份规范的 0x20 字节时间状态到栈上，再令栈上
  `outputResult+0x08` 指向该副本；result 的其余数组使用当前 evaluation context 的
  临时分配器，并在每个候选后回收；
- 直接秒数采样路径把时间状态 `end(+0x10)` 与 `start(+0x18)` 同时写成
  `sampleFraction * descriptorDuration`，随后以 `mode=1`、`timeScale=1`、
  `evaluateExtraChannels=1` 调用 evaluator。相等端点形成零长度 motion 区间，同时
  得到该任意时刻的骨骼姿态供误差计算。

这确认 evaluator 在调用期间同步消费时间状态，不会要求该 0x20 字节对象必须常驻堆；
也确认结果时间区间确实能够表达任意 descriptor 时刻。边界同样明确：规范 scratch
构造依赖活动 Graph 的模板、TLS evaluation context、临时分配器和配套回收，不能把
裸函数当成可在 RideOff 状态代码中脱离动画求值上下文调用的独立 endpoint API。
上述入口、栈结果指针、端点写入、调用和上下文门均已在 IDA 中注释。

## 2026-07-26 末端区间求值：视觉成立、操控失败

自动测试样本 `artifacts/boarding/dismount_20260726_115305_099` 已确认实际 RideOff
长结果满足精确运行门：

```text
session=1
callerRva=0x366F423
mode=0
timeScale=1
evaluateExtraChannels=1
mappedPhase=0
interval=0..0.00834168->2.1021
```

实现保留 `start(+0x18)=0`，只把 `end(+0x10)` 及其 float 镜像扩展到
`descriptor+0x3C` 的 `duration=2.1021`，随后只调用一次原 evaluator。返回后的原生
结果为：

```text
duration=2.1021 sync=2.1021 complete=1
```

按 `capture_manifest.csv` 的实际中点，117ms 已完全离开座位并位于车旁落地轨迹，
320ms、726ms 均无车辆下车姿态或车顶错误落点；这些静态帧不能判断玩家控制是否恢复。
由此确认这个 Graph 求值调用能够消费完整剩余时间区间并生成视觉末端；它无需构造脱离
活动 Graph 上下文的 scratch 结果，也无需直接写玩家世界坐标，但后续操控证据证明
这种消费并不安全。

但该关键帧结论并不等于功能完成。用户后续实际操控确认，下车后玩家动作完全卡住，
无法执行任何动作。这证明一次性消费完整剩余区间虽然生成了正确视觉末端和空间位移，
却没有让玩家动作状态机恢复到可继续接受动作的状态。该候选已经判定失败并撤回，
不得通过视觉落地或 `complete=1` 将其重新认定为成功。当时源码、工程和测试脚本曾
移除 `RideOffGraphEndpoint` 并恢复 328192 字节诊断基线；当前重新加入的是本文件末节
记录的同步渐进推进实现，不是这一严格末端候选。

### `mode=0` 的严格越界完成条件

DS2 evaluator 的汇编给出了失败日志中 `sync=duration` 但 `end=0` 的精确原因：

- `0x14219A904` 首先清零 `timeState+0x0E`；
- `0x14219A9F1` 用 `VCOMISS` 比较请求区间终点与 `descriptor+0x3C` duration；
- 请求终点小于或**等于** duration 时，`JBE` 直接跳过完成块；
- 只有请求终点**严格大于** duration，`0x14219A9F8` 才把实际求值终点钳制为
  duration，`0x14219AA01` 随后写 `timeState+0x0E=1`。

旧实现把请求终点写成恰好 `2.1021`，与 duration 相等，所以 evaluator 可以生成末端
姿态和完整 motion transform，却明确不会写原生 `reachedEnd`。日志中的
`complete=1` 只是 Mod 以 `sync>=duration` 计算的派生值，不是引擎完成事件；原生日志
`end=0` 才是这一边界的权威结果。该事实证明当前实现漏掉了原生完成语义，但
`reachedEnd=1` 是否单独足以恢复玩家操控仍需由其实际消费链闭合，不能提前写成修复
结论。上述比较、钳制和 flag 写入点均已在 IDA 中添加注释。

对 DS2 `ActionGraph_EvaluateDescriptorToResult` 整个函数边界的指令级复核还确认：
函数本体没有读写输出 `ActionGraphResult+0x50/+0x54`。函数内仅有的
`0x50/0x54` 位移都属于栈局部矩阵；它调用的 `sub_142198380` 只查找或分配结果 item，
`sub_14219B620` 只把该 item 的 48 字节 payload 写回目标槽，也不改这两个 result
元数据字段。因此严格越界只会在这里建立封顶时间、`reachedEnd` 和求值 payload，
不会直接生成 fullgame 顶层控制器消费的同步区间重基准覆盖值；`result+0x50` 必须来自
evaluator 外部的结果传播或状态机逻辑。

同一 evaluator 在写完 `result+0x4C` 后还会调用现已命名的
`AnimationDescriptor_CollectActiveEventTagsForInterval`（`0x1403254B0`）。
该函数遍历 `descriptor+0x50` 所指事件表中的 16 字节区间项，按
`start/duration` 与本次采样区间的重叠关系去重收集 tag payload。严格越界候选提交的
`0..duration` 区间会正常经过这条原生 tag 收集路径；它不是只采末端 pose/root motion
而完全跳过 descriptor 自身事件。冻结缺口因此位于这些叶 payload 之后的外层消费或
动作所有权维护链，不能再解释成 evaluator 没有执行自己的事件遍历。

DS2 内部已经确认至少一处实际下游消费者。`sub_1421A8420` 在
`0x1421A853B` 读取来源结果的 `timeState+0x0E`；该位为 1 时，它改用
`timeMap+0x10` 的终端值再进行 phase 映射，为 0 时则继续使用普通区间终点。由此确认
`reachedEnd` 会改变后续 Graph 同步/区间推进，不是仅供日志或统计使用。该读取点也已
加入 IDA 注释；它证明旧实现丢失了一个真实下游语义，但尚不把单一消费者扩大解释为
全部玩家输入锁的唯一释放点。

fullgame 的 RideOff 长结果选择器还确认了一个容易混淆的传播细节。两处选择结果分别在
`0x183670045` 和 `0x183670AC7` 以 mask `0x73` 调用
`ActionGraphResult_MergeChannelsByMask`。`0x73` 的二进制值为 `1110011`：

- bit `0x20` 会复制来源 `timeState+0x0E reachedEnd`，以及 result 的
  `+0x38/+0x48/+0x50/+0x56` 元数据；
- bit `0x04` 才会调用 `ActionGraphResult_PropagateSyncFrame`，复制来源同步帧与
  区间；`0x73` 不包含该位；
- bit `0x08` 才会复制来源 sync-map 指针；`0x73` 同样不包含该位。

首层两处选择是平行分支；任一实际路径随后还会经过 `0x183671090` 或
`0x1836716D4`、`0x1836740C4`，最后在 `0x1836742D3` 合并到该子图的调用方结果。
这四级实际传播全部使用 `0x73`。因此每一级都会把选中结果的 `reachedEnd` 传入目标，
却保留目标原有的同步区间和 sync-map。`ActionGraphResult_PropagateSyncFrame` 本身
也不复制 `reachedEnd`，两类状态在该 ABI 中就是两个独立通道。这个结论证明“日志中的
长结果已经到末端”和“最终结果采用了同一长结果的时间区间”不能画等号；目前尚没有
证据把这种分通道合并单独认定为玩家冻结的充分原因。相关 helper 分支和 RideOff
传播调用点均已加入 IDA 注释。

这四级合并仍不是玩家顶层 Graph 的最后边界。外层 `sub_183605C47` 在
`0x1836070D2` 确认当前动态表条目 key 为 `0x0184F189` 后，于 `0x183607217`
调用该子图；子图已据此命名为
`PlayerActionGraph_Subgraph_0184F189_Evaluate`。它把最终结果写回动态表条目的
`entry+0x38`。定点控制流和运行时返回地址共同纠正了旧归属：从 `0x183607217`
返回后会经无条件跳转离开该局部表，不会顺序落入 `0x183607896`；后者属于 key
`0x4404D873` 的另一张局部动态表。实际 RideOff 路径最终在公共出口
`0x18360CCEB` 调用 fullgame 运行时解析槽
`ActionGraphDynamicTable_PostEvaluate_Ptr`，精确返回地址为 `0x18360CCF1`。
该调用以 `RCX` 传动态表、`XMM1` 传帧增量、`R8` 传当前 `entry+0x38`。
`CoreLibraryInitializer` 以哈希 `0x49589132` 解析这个槽，fullgame 文件内没有其实现。
通用动态表 evaluator `sub_1801D0828` 也在求值全部条目后调用同一槽，把调用方
ActionGraphResult 作为输出，随后才清理表状态。

对 `PlayerActionGraph_Subgraph_0184F189_Evaluate` 的精确控制流复核进一步确认：
从 RideOff 叶 evaluator 返回点 `0x18366F423` 到第一层选择合并返回点
`0x183670ACC` 的最短实际路径共有 27 个基本块、96 条指令。路径只执行结果引用计数、
work-context 活动门、一次较低层动态表 post-evaluate、候选选择和 `0x73` 合并；
没有读取 RideOff 叶结果的 timeState、`reachedEnd` 或 `evaluationEndSeconds`
来决定是否退役当前 action。该路径读取的 `r12+0x4C` 属于 work context，
不是叶 `ActionGraphResult+0x4C`。继续追到本子图最终 `0x73` 合并点
`0x1836742D8`，其间仍只有同类活动门、动态表 post-evaluate 和结果选择。
因此末端标志确实会由子图向外传播，但 RideOff 子图本身不消费它来释放动作所有权；
完成事件或所有权释放必定位于该子图之外。上述三个关键边界均已加入 IDA 注释。

子图之外已经找到另一个不同的结果标量消费者。顶层控制器迭代 exact RideOff 队列时，
`entry+0x88` 正是当前项 `ActionGraphResult+0x50`；`0x18339D608` 检查它是否
非负。命中后，`0x18339D620` 所在路径会重置一个下级临时结果的 sync-state
generation/frame、置瞬时标志并清该临时结果自身的 `result+0x50`，generation
不匹配时才调用 `qword_1884D2128` 维护下级队列。fullgame
`ActionGraphResult_PropagateSyncFrame` 又确认：只有来源 `+0x50` 非负且目标
`+0x54` gate 有效时才复制该值；遇到 frame generation 不连续时，以
`max(+0x50, 0)` 重建同步区间起点。因此 `result+0x50` 的精确用途是跨层同步区间
重基准，并且与 `evaluationEndSeconds(+0x4C)` 不同；但现有失败样本
没有记录顶层 `entry+0x88`，不能据此断言候选命中过该分支，更不能提前把它写成修复值。

DS2 宿主又闭合了这个字段的产生端。`ActiveStatesQueue_PushActiveState` 在转移属性
`+0x0D` flag 有效时，把属性 `+0x08` 写入新队列项 `entry+0x98`；
`ActionGraph_StatesQueueEvalLogic` 只在该值大于零时把它一次性输出到
`ActionGraphResult+0x50`，同时编码 `result+0x56` 并清零 `entry+0x98`。
descriptor evaluator 本体及其结果-item helper 均不写 `+0x50`。因此它是由原生状态
转移属性建立的同步重基准请求，不是 descriptor 到达末端后自动产生的完成字段。

对该 exact RideOff 分支所有当前顶层项引用的复核还排除了另两条完成输入。它直接消费的
只有 `entry+0x40 = result+0x08` 所指 timeState 的 frame generation／瞬时标志、
`entry+0x88 = result+0x50` 以及 `entry+0x8C = result+0x54` gate；没有读取
timeState `+0x0E reachedEnd`，也没有读取
`entry+0x80/+0x84 = result+0x48/+0x4C` 来退役 top state。因此末端标志的动作
所有权释放消费者既不在 RideOff 叶子图，也不在 `sub_18339A856` 的顶层 RideOff
控制分支；这两层已加入 IDA 注释。

RideOff 表的实例指针现也已从顶层求值入口闭合。`PlayerActionGraph_EvaluateFrame_Stage1`
在 `0x183162E41` 以 `lea rax, [r8+0x5730]` 取得玩家 Graph 运行时状态块中的固定
子对象，并将其写为 Stage2 的 `arg_69B0`。Stage2 在 `0x1831D0E10` 把该参数写入
调用 `sub_183594DF9` 所用的栈参数位置；后者于 `0x183595F35` 从 `arg_5B8`
读取同一指针，再把对应局部变量 `v1912` 作为 `sub_183605C47` 的第 17 个参数传入。
这正是 `sub_183605C47` 中执行 `0x0184F189` 分派，并最终到达公共出口
`0x18360CCEB` post-evaluate 的队列。

Stage2 还在 `0x18318C135` 直接读取同一个 `arg_69B0`，以
`RCX=queue`、`EDX=0x78EC48BF`、`R8=0` 调用 `qword_1884D2108`。初始化器在
`0x18001AFB1` 以 hash `0x6685E865` 解析该槽。通用动态表 evaluator
`sub_1801D0828` 同样在进入条目求值前以
`(queue, stateKey, transitionProperties)` 形态调用此槽；因此这里确认的是
`0x78EC48BF` 默认状态被初始化／提交到 `base+0x5730` 的持久状态队列，而不是叶
wrapper 临时结果。后续定点调用链已经证明该槽不是运行时状态间转移入口；真正的
转移槽是相邻的 `qword_1884D2100`。`0x78EC48BF` 的具体玩法名称尚未由直接证据
闭合，不能仅凭它是默认状态就提前命名为 Free。相邻但不同的 `qword_1884D2088` 由 hash
`0x0E350626` 解析；其 Stage2 调用形态是从对象返回内部指针，不是这次状态队列提交，
两槽不得混用。

### `base+0x5730` 的顶层状态转移入口

同一个 Stage2 `arg_69B0` 还沿另一条生成式调用链进入该队列的顶层状态机逻辑：

```text
Stage2 arg_69B0
  -> outgoing arg_6468 -> sub_1831DB718
  -> outgoing arg_2300 -> sub_1832D17BC
  -> outgoing arg_1A0  -> sub_18333FCAC
  -> outgoing arg_C8   -> sub_18339A856
```

各次传递已分别在 `0x1831B4183/0x1831B418B`、`0x1831E1464/0x1831E146C`、
`0x1832D6629/0x1832D6631` 和 `0x1833419C0/0x1833419C8` 闭合；四个下游调用点
依次为 `0x1831C28FC`、`0x1831EAD44`、`0x1832D68D9` 和 `0x183341AFC`。
因此 `sub_18339A856` 的 `arg_C8` 与前述求值、post-evaluate 路径使用的是同一个
`base+0x5730` 持久状态队列，不是名称相似的另一张表。

`sub_18339A856` 读取该队列最后一个步长 `0xB8` 条目的 `+0x30` 状态 key，并能区分
`0x0BC4A758`、`0x4404D873`、`0x78EC48BF` 和 RideOff
`0x0184F189`。它构造 0x20 字节 `TransitionProperties` 后，以
`(queue, targetStateKey, transitionProperties, 0, context)` 调用
`qword_1884D2100`。在 `0x18339ABA8` 至 `0x18339ABB3`，`RCX` 明确是上述 exact
queue，`EDX` 明确设为 `0x0184F189`，从而确认这是进入 RideOff 的顶层状态转移调用。
同一公共调用点的其他分支会提交 `0x0BC4A758` 或 `0x4404D873`。

通用转移 helper `sub_1801D1E2F` 也会先检查当前条目的过渡比例和条件，再构造相同的
0x20 字节属性并调用 `qword_1884D2100`。这交叉确认 `qword_1884D2100` 才是实际
状态间转移槽，而 `qword_1884D2108` 负责初始／默认状态提交；相邻
`qword_1884D2128` 是另一条逐状态维护调用，当前证据不足以给它命名。至此已经找到
失败候选未触及的顶层状态转移层，但 RideOff 转出的条件、目标 key 和过渡属性尚未由
当前证据闭合，因而这里不记录任何修补方案。

继续沿同一函数的当前状态预处理分支复核后，确认顶层队列当前 key 已经是 RideOff
`0x0184F189` 时，不会在该预处理块中对 exact `arg_C8` queue 再调用
`qword_1884D2100`。代码只显式处理当前 key 为 `0x0BC4A758`、
`0x4404D873` 或 `0x78EC48BF` 的转移条件；RideOff 会从
`0x18339A950/0x18339A957` 直接落到 `0x18339ABBB` 的活动条目遍历。随后
`0x18339B49B` 识别 RideOff 条目并进入 `0x18339C031` 的专用分支。

RideOff 分支内可达的其余 `qword_1884D2100` 调用属于更深层状态机：已定点看到的
队列实参来自 `arg_AB0`、`arg_1E0`、`arg_1E8`、`arg_1F0`、
`arg_A58`、`arg_A88` 等下级参数或其保存寄存器，而不是再次从 `arg_C8` 取顶层
exact queue。逐级反向追踪这些参数到 Stage1 后，已经确认其中至少六张队列是同一
Graph 运行时基址下彼此不同的固定子对象：

```text
base+0x5750 -> 0x18339D950，目标 0x30476340 或 0x37C2D311
base+0x5770 -> 0x18339DEBF，目标 0x47F277CD
base+0x5790 -> 0x18339E02E，目标 0x423A3A6B
base+0x58F0 -> 0x18339D78F，目标 0x3502C7A3
base+0x5910 -> 0x1833A20FE，目标 0x476944A0
base+0x5930 -> 0x1833A31BD，目标 0x2EE1054C
```

这些指针分别由 Stage1 固定 `lea` 产生，再经 Stage2、`sub_1831DB718`、
`sub_1832D17BC` 和 `sub_18333FCAC` 的栈参数原样转发；因此它们与
`base+0x5730` 顶层队列的不同不再只是参数名差异。因而不能把这些调用直接解释为
“顶层 RideOff 转回默认状态”。这把冻结缺口进一步收窄到 RideOff 内部动作／嵌套
状态队列的完成链。

长 descriptor 与其中一组下级状态块的静态关系现已进一步闭合。Stage1 在
`0x183161005` 取得 `base+0x58F0`，随后经 Stage2 `arg_7398`、
`sub_183594DF9 arg_640`、`sub_183605C47` 第 34 个参数，最终在
`0x18366ED69` 成为 `PlayerActionGraph_Subgraph_0184F189_Evaluate` 的
`R12/work` 基址。`0x18366F41D` 的 2.1021 秒长 descriptor 只在
`work+0x22` 或 `work+0x13` 非零时求值；这两个原始 gate 分别落在
`base+0x58F0` 起始块及其相邻 `base+0x5910` 块的范围内。该映射纠正了“尚未与
任何下级块对应”的旧表述，但尚未闭合两个原始字节的字段语义，也没有证明
`base+0x58F0` 的某次 `qword_1884D2100` 转移就是动作释放，因此仍不能重放任意转移。

通用 helper `sub_1801D1E2F` 的反编译还闭合了转移槽的完整调用约定：
`qword_1884D2100(queue, targetKey, transitionProperties, extra, context)` 返回一个
指针值；调用方会原样返回该值。该 helper 的多条原生转移条件会检查当前条目
`+0x80/+0x84` 的过渡比例或事件编号，再选择目标 key 和对应 `extra`。所以观察或包装
此槽时必须保留五个参数和返回值，不能把它简化成三参数无返回函数。

### 2026-07-26 原生转移槽运行观察

安全基线样本 `artifacts/boarding/dismount_20260726_155144_488` 使用完全透传的
`qword_1884D2100` 槽观察器，两个唯一 fullgame 调用点解析到同一运行时槽
`0x7FFCCA4D2100`。观察器保留五个参数和原返回值，只在 RideOff 五秒会话内记录，
每会话预算 96 条；本轮预算没有耗尽。

会话内只出现一次转移调用，时间为 RideOff OnEnter 后约 10ms，返回点
`callerRva=0x339ABB9` 与静态 `0x18339ABB3` 调用完全对应：

```text
before: count=1 key=0x4404D873 clock=3.09894 flag=0
target: 0x0184F189
after:  count=2 key=0x0184F189 clock=0 flag=1
result: 0x2
```

这直接确认了顶层 Drive -> RideOff 的入队行为。此后直到五秒观察窗结束，没有第二次
`qword_1884D2100` 调用；进程于 `15:51:52.862` 正常记录两条
`DLL_PROCESS_DETACH`。结合安全基线约 2 秒后原生动作自行结束的既有视觉证据，可以
证伪“原生完成时还会重放某一条下级 `qword_1884D2100` 转移，而失败候选只是漏掉
它”这一解释。下一层调查必须转向不经过该转移槽的队列收尾／动作所有权释放路径，
不能任意选择上节某个下级 target key 进行重放。

三个不同调用点共同确认其 ABI 为
`PostEvaluate(dynamicTable, deltaSeconds, outputResult)`：第 2 参数从 `XMM1` 传入，
第 3 参数位于 `R8`。另一个通用 helper `sub_1801D61D0` 让该槽先填充/更新临时
ActionGraphResult，返回后立即再以 `0x73` 把临时结果向上合并。因此它不是无返回值的
表清理通知，而是位于动态条目求值与上层结果之间的实际结果处理边界。

失败候选只包装叶 `ActionGraph_EvaluateDescriptorToResult_Ptr` 并改写该叶
`timeState` 的请求区间；它没有改动外层 `PostEvaluate` 调用。定点汇编与运行命中确认，
实际 RideOff 调用 `0x18360CCEB` 仍把原始 Graph 单帧增量传入 `XMM1`。因此失败帧中
同时存在两个都真实但不同层级的时间推进：

```text
RideOff 叶 descriptor：约 0.0125s -> 2.1021s
动态表 PostEvaluate：仍只接收该帧原始 deltaSeconds
```

这证明候选只把姿态/根运动叶推进到末端，没有同步推进包裹它的动态表处理。该分层与
“视觉和落点立即完成，但动作所有权仍不释放”一致。DS2 宿主实现现已在下节完整闭合；
它确认 `deltaSeconds` 只推进队列时钟，本身不是一个动作退役 API。

传给 `PostEvaluate` 的第 3 参数也不是原始叶结果。`0x1836742D3` 的最终 `0x73`
合并目标就是当前动态条目的 `entry+0x38`，随后公共出口 `0x18360CCEB` 把同一个
`entry+0x38` 作为 `outputResult` 交给宿主槽。由于最终合并不包含 `0x04/0x08`，
该条目收到的是叶 `reachedEnd` 和内容/payload，保留的却是条目自己原有的同步区间与
sync-map。也就是说宿主 post-evaluate 从未直接收到 wrapper 改写过的叶 timeState
指针；现有叶日志无法显示它实际看到的条目区间。

因此，wrapper 日志中的 `end=1` 权威地证明了 **RideOff 叶 descriptor** 已到末端，
但不能证明动态表 post-evaluate 边界或玩家顶层结果收到了与其同步区间一致的终态。
冻结发生前还存在这一层宿主处理，旧日志没有观测它的输入输出。该调用槽、解析哈希和
RideOff 调用点已在 IDA 中命名/注释；下节的 DS2 实现确认它会合成结果与 event/tag
活动性，但不会释放当前状态。

### DS2 `StatesQueue` 宿主实现

DS2 的动画导出注册表已经把 fullgame 中的两个运行时槽闭合到具体实现：

- `AnimationData::sExportedStatesQueueUpdateTime` 对应
  `ActionGraph_StatesQueueUpdateTime`（`0x14219DE00`），ABI 为
  `UpdateTime(statesQueue, inputResult)`；
- `AnimationData::sExportedStatesQueueEvalLogic` 对应
  `ActionGraph_StatesQueueEvalLogic`（`0x14219F880`），其寄存器用法与 fullgame
  三处调用共同确认的 ABI 完全一致：
  `EvalLogic(statesQueue=RCX, deltaSeconds=XMM1, outputResult=R8)`。

`StatesQueueEvalLogic` 在 `0x14219F8C7` 只把传入的 `deltaSeconds` 累加到最新
`0xB8` 字节队列项的 `entry+0xA0`。随后它以 mask `0x73` 合并条目结果，并在
`0x14219FEB0` 把合成结果写入调用方 `outputResult`。这直接确认失败候选把 RideOff
叶一次推进约 2.1 秒时，DS2 的最新状态队列项仍只推进了一个原生帧；fullgame
参数分析所见的两条时间线不是 ABI 猜测。

该函数末尾调用的 `0x14219E5B0` 已命名为
`ActionGraph_StatesQueue_MergeEventTagActivity`。它逐 result item、逐活动队列项
合并 16 字节 event/tag 记录及当前／过渡活动标志，并把结果发布到
`ActionGraphResult+0x38`。它不删除队列项，也不改 count、state key 或状态时钟。
因此 fullgame `0x18360CCEB` 的宿主 post-evaluate 边界已闭合为“推进状态时钟、求值并
合并 `0x73` 结果、输出一次性同步重基准、合并 event/tag 活动性”；其中没有隐藏的
当前 action 退役步骤。真正的 RideOff 动作释放判断位于生成式 fullgame 逻辑。

内部 helper `0x14219F7D0` 会读取条目结果的 `timeState+0x0E reachedEnd`。该位为
0 时，它可对条目时间取模；该位为 1 时则保留终端时间，然后把派生值写入
`entry+0xA4`，并可经 time-map 写入 `entry+0xA8`。这个 helper 不删除队列项。
因此严格越界候选的 `reachedEnd=1` 确实进入了宿主逻辑，但在这个阶段只改变终端时间
派生，不能单独完成动作项退役。

实际队列擦除位于 `StatesQueueUpdateTime`。它先通过
`ActionGraphResult_GetTimeIntervalLengthSeconds(inputResult)` 取得输入结果的时间
区间长度，用该长度推进旧队列项的过渡计时；满足结束条件时，
`0x14219E427` 调用 `0x1402389A0`。后者已确认是步长 `0xB8` 的数组区间擦除：
逐项释放资源、`memmove` 后续项并减少队列计数。由此可以确定：

```text
叶 reachedEnd / 终端姿态
    != StatesQueue 最新项累计时间
    != 旧状态项的过渡计时与数组退役
```

这解释了为什么 `end=1`、正确根运动和正确车旁落地可以同时成立，而玩家动作仍保持
占用。该阶段已经确认失败候选造成了这三层不同步；后续通过同步推进叶与外层队列时钟，
并保留终点前的原生交接阶段解决，未直接写入任何未知完成字段。

`ActiveStatesQueue` 的导出实现还确认了当前状态项的维护规则。
`ActiveStatesQueue_PushActiveState`（`0x14021F8B0`）按 `entry+0x30` 的状态哈希
查找步长 `0xB8` 的队列项：目标已是最后一项时会重激活它，目标位于更早位置时会先
移除旧位置再追加，未命中时直接追加。追加新状态前，传入的 0x20 字节
`TransitionProperties` 会写到原最后一项；新项初始化后保存新状态哈希，并把
`entry+0xA0` 清零。同一当前状态重激活时也会把 `+0xA0` 清零。结合
`StatesQueueEvalLogic` 每帧对 `+0xA0` 加 `deltaSeconds`，可以确认它是从当前状态
激活开始累计的独立状态时钟，而不是 descriptor 的叶播放头。

DS2 的 `SetCurrentStateEventSpaceTimeInSMContext`（`0x1421BC7C0`）精确读取当前
队列项 `result+0x18` time-map 和 `result+0x4C` evaluation-end 秒数，把从零开始
推进该秒数所得的映射值写入 `queue+0x10`；它不读取 `entry+0xA0`。
`ActionGraph_EvaluateDescriptorToResult` 已确认 `result+0x48` 是
`descriptorDuration/timeScale`，`result+0x4C` 则是包含完整循环数及当前周期封顶
末端的当前动作末端秒数，非循环 RideOff 严格越界时两者才相等。

结果合并函数又确认 mask bit `0x20` 会无条件复制 `result+0x48/+0x4C`；原生四级
`0x73` 已包含该位。严格越界样本第一帧的顶层队列日志也已经是
`derived=2.1021, mapped=2`，所以此前根据 double 区间日志推断“外层 `+0x4C` 仍为
单帧值”不成立。`0x7F` 实验只证明 bit `0x04` 的同帧 timeState 区间传播没有覆盖
目标。由此可以确认“状态激活时钟只推进一帧”和“状态机事件时间已到动作末端”能够
同时成立；后者已经成立却仍未释放 RideOff action。

对应导出 `ActiveStatesQueue_RemoveActiveStates`（`0x1421959B0`）把索引加一后调用
原生区间擦除器，因此其精确语义是删除 `[0, index]`（含 index）的活动状态。它与
`StatesQueueUpdateTime` 自动清理旧过渡项使用同一个资源释放和数组搬移实现。当前尚未
确认 RideOff 转出条件单独读取 `+0xA0`，还是还依赖另一条 Graph 事件；现有实现也没有
把该字段直接写成解锁值，而是仅把实际传给宿主的帧增量增加相同的叶加速量。

### `reachedEnd=1` 仍未释放玩家动作

严格越界运行样本 `artifacts/boarding/dismount_20260726_122349_721` 已把上一节的
边界条件实际闭合。实现没有直接写完成位，而是把请求终点设为 duration 的下一个可
表示 float；原 evaluator 随后自行钳制并返回：

```text
duration=2.1021 sync=2.1021 end=1 complete=1
```

同一 descriptor 的下一帧仍为 `sync=2.1021 end=1`，所以完成位没有在 wrapper 返回后
立即丢失。133ms、320ms、718ms 关键帧继续证明视觉动作与车旁落点正确。

但本轮测试在 800–1700ms 自动按住 W。`capture_manifest.csv` 的实际中点为 718ms、
1218ms、2031ms；三帧中玩家位置和冻结的倾斜姿态一致，说明按键发送成功期间仍没有
移动响应。由此证伪“旧候选只缺 `reachedEnd`”这一更窄解释：该位有真实下游语义，
但它单独不足以完成当前活动 Graph action 的退栈、所有权释放或玩家输入解锁。现有
日志还没有把这些后续状态逐项命名，因而不能把其中任一未知字段提前写成修复入口。

该 334336 字节候选 SHA-256 为
`9418291192221BA395F3EAB1518C0C6C5C13598B92B61C5DDD69AE06FACB1432`，
已经撤回部署；当前部署状态以主索引的“当前部署状态”节为准。

本次日志还证伪了把 `outputResult+0x40` 直接当成 motion item 指针并从其
`+0x10/+0x14/+0x18` 读取平移的解释：该诊断得到约 `3.02e23` 量级的无效值，与画面
和原生物化结果均不相符。代码已移除这组误导读取。有效的空间传播证据仍是
`GraphAnimationManager_EvaluateFrame` 与
`AnimationGraph_MaterializeResultToPoseContext` 所确认的标准结果物化链，不能把
顶层 `outputResult+0x40` 自行命名为可直接解引用的 motion item。

### 同一 RideOff key 分布在三张独立动态表

`sub_183605C47` 对状态 key `0x0184F189` 有三个独立比较点和三套队列／结果：

```text
0x183606747 -> 0x183606D6A
  descriptorPack+0x35A8, mode=0, evaluatePose=0

0x1836070D2 -> 0x183607217
  PlayerActionGraph_Subgraph_0184F189_Evaluate
  内含已验证的 2.1021s 姿态 descriptor

0x183609DB6 -> 0x183609EF1
  descriptorPack+0x3538, mode=0, evaluatePose=0
```

第一、第三条各自把结果写入本动态表当前 `0xB8` 队列项的 `entry+0x38`，并走各自的
`ActionGraphDynamicTable_PostEvaluate`；它们不是中间姿态子图的重复调用。由此确认
RideOff 的生成图结果至少分为一条姿态层和两条非姿态伴随层。此前只推进中间长
descriptor 的失败候选没有推进另外两层。

但运行样本 `dismount_20260726_185846_956` 已排除把这两个静态分支纳入快速下车
实现。修正会话观测范围后，RideOff 窗口共记录 70 次精确中间姿态调用和 32 次其他
Graph descriptor 调用；`0x183606D6A`、`0x183609EF1` 两个非姿态同-key call site
均为零命中。因此三处虽然共享状态 key，只有中间表在本次实际 RideOff 路径活动；
冻结不是因为旧候选漏推进这两条未激活 descriptor，不能把它们加入 Mod。

同一样本还首次记录了自然末端后的精确 evaluator 输入。跨过 2.1021 秒后，中间
 descriptor 仍继续逐帧调用；其输入 timeState 区间终点从 2.11045 秒继续增长到
2.4024 秒，而输出始终为 `duration=sync=2.1021`、`reachedEnd=1`。因此自然完成
不会停止该 descriptor 的求值，也不会把输入播放头固定在 duration；它依赖的是继续
前进的外层时间线。三个比较点、两个伴随求值点和实际零命中限定均已记录。

## 2026-08-23：同步推进与原生终点前交接

当前实现只处理运行时精确识别的 RideOff 长 descriptor。每次求值把请求区间终点最多
增加 `0.25s`，但上限是 `duration - 0.1s`；实际增加量同时暂存在线程局部会话中。
同一帧到达 `0x18360CCEB` 时，动态表 wrapper 只对匹配会话和目标表把这份增加量加入
原始 `deltaSeconds`，随后调用原函数。这样叶播放头和最新状态项 `+0xA0` 时钟按同一
额外量前进，而不是只让根运动姿态跳到末端。

通过样本 `artifacts/boarding/fast_dismount_20260823_231008_715` 的四次已记录推进为：

```text
elapsed=15ms   leaf 0..0.0125125 -> 0.262513   table delta 0.0125125+0.25
elapsed=484ms  leaf 0.733817..0.742159 -> 0.992159
elapsed=750ms  leaf 1.23824..1.24658 -> 1.49658
elapsed=844ms  leaf 1.58834..1.60085 -> 1.85085
```

最后一次仍低于 `duration=2.1021` 的原生交接上限 `2.0021`；实现没有伪造严格
`reachedEnd`，也没有在 mover 消费根运动前手动调用 operation 21 或把 pending
`3 -> 0`。在 `elapsed=1094ms`，原生代码依次从 RideOff OnExit 调用点
`0xF97B56`、RideVehicleActionPlugin OnExit 调用点 `0x1004F88` 和 Basic OnEnter
调用点 `0xFB40B6` 请求动画状态 `1`，三次记录均位于同一世界坐标
`851.7032313,-4017.35567,102.2153168`。因此退出发生在原生允许的终点前窗口，
不是 Mod 强制完成后再模拟状态切换。

测试随后按住 S，并在目标 `25/50/100/200/400ms` 的短窗口取帧；角色在 100ms 已有
明确转身/迈步，400ms 已离开原位置。日志中没有 Fall 入口调用点 `0x110694D`。
该样本最终返回 `FAST_DISMOUNT_TRACE_OK`，证明当前卡车驾驶位路径同时满足视觉加速、
原生动作释放、地面落点和输入恢复。当前已验证 ASI 为 343552 字节，SHA-256：
`56D19BE0DAFE7898BA2DA82AEF1575AE71A8677775502ABBC3FC42FDAF8E12E0`。
