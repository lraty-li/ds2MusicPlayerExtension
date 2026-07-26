# DS2 快速下车：fullgame 状态队列运行时证据

日期：2026-07-26

本文只记录已经由定点静态分析和运行日志确认的 fullgame `ActiveStatesQueue` 行为。
快速下车仍未完成；本文不记录待定修补方案。

## 已确认的队列布局

`PlayerActionGraph_EvaluateFrame_Stage1` 使用同一 Graph 运行时基址中的七张队列：

```text
base+0x5730  顶层玩家状态队列
base+0x5750  RideOff 分支下级队列
base+0x5770  RideOff 分支下级队列
base+0x5790  RideOff 分支下级队列
base+0x58F0  RideOff 分支下级队列
base+0x5910  RideOff 分支下级队列
base+0x5930  RideOff 分支下级队列
```

`base+0x58F0` 还具有已确认的生成图工作块角色。Stage1 取得该固定指针后，经
Stage2 `arg_7398`、`sub_183594DF9 arg_640` 和 `sub_183605C47` 第 34 个参数，
最终成为 RideOff 子图的 `R12/work`。2.1021 秒长 descriptor 的求值 gate 是
`work+0x13` 与 `work+0x22`；二者分别处于 `base+0x58F0` 起始块和相邻
`base+0x5910` 块范围内。现有静态证据只闭合地址归属，不给这两个原始字节命名，
也不把任一已知下级转移认定为解锁调用。

七个对象均为步长 `0xB8` 的活动状态队列。最新项的已确认字段为：

```text
entry+0x30  状态 key
entry+0x88  当前项 ActionGraphResult+0x50 的同步区间重基准覆盖值
entry+0x9D  转移标志
entry+0xA0  从当前状态激活开始累计的独立时钟
entry+0xA4  派生时间
entry+0xA8  映射时间
```

fullgame 顶层控制器还确认了 `entry+0x88` 的一个实际消费者。当前 key 为
`0x0184F189` 时，`0x18339D608` 检查该标量是否非负；命中后可在
`0x18339D620` 清零对应临时结果 sync-state 的 generation/frame 字段、置瞬时标志，
并把该临时结果自己的 `result+0x50` 清零。随后 generation 不匹配才会调用
`qword_1884D2128` 维护一张下级队列。由此确认 `result+0x50` 与
`evaluationEndSeconds(+0x4C)` 是两个不同的外层控制通道。同步传播 helper 还确认：
仅当来源 `+0x50` 非负且目标 `+0x54` gate 有效时才复制它，帧 generation
不连续时会用 `max(+0x50, 0)` 重建同步区间起点。现有日志没有记录失败候选
的 `entry+0x88`，所以这里不推断它当时是否命中，也不把该字段写成解锁开关。

同一 exact RideOff 分支没有读取当前顶层项 timeState `+0x0E reachedEnd`，
也没有读取 `entry+0x80/+0x84` 的 duration／evaluation-end 来决定退役。它对当前项
的直接时间控制输入仅是 timeState generation／瞬时标志、`entry+0x88` 重基准和
`entry+0x8C` gate。由此排除顶层状态控制器直接以叶完成位或末端秒数释放玩家动作；
释放链仍位于这些已确认分支之外。

DS2 宿主现已闭合 `result+0x50` 的上游来源。`ActiveStatesQueue_PushActiveState`
只在 `TransitionProperties+0x0D` flag 有效时，把 `TransitionProperties+0x08`
保存到新条目的 `entry+0x98`。`ActionGraph_StatesQueueEvalLogic` 在帧末检查最新
条目的这个一次性字段；它大于零时才把数值复制到输出 `result+0x50`、把类型编码进
`result+0x56`，随后立即清零 `entry+0x98`。因此 `result+0x50` 是状态转移属性产生的
一次性同步重基准请求，不由 descriptor evaluator 的严格越界或 `reachedEnd`
生成，也不能把它自行伪造成动作完成信号。

顶层进入 RideOff 时，转移槽 `qword_1884D2100` 向 `base+0x5730` 追加
key `0x0184F189`，队列计数从 1 变为 2，并把新项 `+0xA0` 清零。

## DS2 宿主 ActiveStatesQueue 导出语义

DS2 的同一导出注册块已经把此前仅按 fullgame 调用形态区分的队列操作闭合为：

- `ActiveStatesQueue_PushActiveState_Thunk` 接收五个参数，并转交
  `ActiveStatesQueue_PushActiveState`；它是实际状态转移 ABI；
- `ActiveStatesQueue_InitializeStatesQueue` 接收
  `(queue, stateKey, context)`，只在空队列上追加首项；
- `ActiveStatesQueue_RemoveActiveStates` 删除队首到指定 index（含）；
- `ActiveStatesQueue_RecycleActiveStatesQueue` 遍历现有条目，回收／清空各
  `ActionGraphResult` 指针并把 `queue+0x14` 置为 `-1`，但不改变 count 或 state key；
- `ActiveStatesQueue_SetCurrentStateEventSpaceTimeInSMContext` 只把最新项
  `result+0x4C` 经过 `result+0x18` time-map 映射后写入 `queue+0x10`；
- `ActiveStatesQueue_ResetQueueOnActivation` 清理队列后重新压入保存的或传入的
  状态；
- `ActiveStatesQueue::sExportedSetBranchNameHash` 在当前 release 构建中与其他空
  函数折叠到同一个 no-op 地址；
- `AnimationStateMachineEvaluationContext_SetTransitionPropertiesInContext`
  只分配 0x20 字节临时块并复制 `TransitionProperties`。

这组宿主实现进一步确认：已有 `qword_1884D2118` 透传观察所见的“count、key 和时钟
均不变”正是 recycle 的原生行为；该收尾槽不是退栈函数。event-space setter 和
branch-name export 同样不具备释放当前动作所有权的副作用。

`ActionGraph_StatesQueueEvalLogic` 的最终内部 helper 也已闭合：
`ActionGraph_StatesQueue_MergeEventTagActivity`（`0x14219E5B0`）只把各活动项
结果中的 event/tag payload 与当前／过渡标志合并到输出 `result+0x38`。它不会删除
当前项或改变队列标量。因此 fullgame 的 post-evaluate 宿主槽并不隐藏一个未观察到的
RideOff 退栈动作；释放条件必须由调用该槽前后的生成式玩法逻辑决定。

## 转移槽运行观察

运行样本：
`artifacts/boarding/dismount_20260726_155144_488`。

五秒 RideOff 会话中，透传观测器只记录到一次 `qword_1884D2100` 调用：

```text
callerRva = 0x339ABB9
queue     = base+0x5730
target    = 0x0184F189
count     = 1 -> 2
clock     = 3.09894 -> 0
flag      = 0 -> 1
result    = 0x2
```

调用点与静态调用 `0x18339ABB3` 一致。原生约 2 秒下车动作完成前后没有第二次
转移槽调用，96 条日志预算没有耗尽，进程正常执行 `DLL_PROCESS_DETACH`。
因此已经证伪“原生动作释放会重放另一条顶层或下级 `qword_1884D2100` 转移”。

## 清理槽运行观察

运行样本：
`artifacts/boarding/dismount_20260726_155902_781`。

`qword_1884D2118` 的调用点和初始化器分别解析到同一个运行时槽
`0x7FFCCA4D2118`。观测器只调用原函数并保留其参数和返回值，没有修改队列。

RideOff 首帧，七张队列的最新项为：

```text
offset  key         clock       flag
0x5730  0x0184F189  0.00834168  1
0x5750  0x37C2D311  3.10311     0
0x5770  0x47F277CD  3.10311     0
0x5790  0x178967C5  0           1
0x58F0  0x2EC81922  0           1
0x5910  0x0869FECA  0           1
0x5930  0x33FBBEE3  0           1
```

每次已记录的 `qword_1884D2118(queue)` 调用前后，队列计数、最新 key、时钟、
派生时间、映射时间和转移标志均相同。六张下级队列在会话中没有记录到上述字段变化。
顶层 `base+0x5730` 保持 RideOff key，并按原生帧推进：

```text
实际日志时间点  +0xA0 clock  +0xA4 derived  +0xA8 mapped
约 100ms        0.100100     0.100100       0.0582525
约 300ms        0.308642     0.308642       0.179612
约 700ms        0.709042     0.709042       0.412622
约 1200ms       1.20954      1.20954        0.703884
约 2000ms       2.00617      2.00617        1.75001
```

本轮日志在 `15:59:10.661` 正常记录模块与 `DllMain` 的
`DLL_PROCESS_DETACH`。由此确认：

- 视觉末端候选把叶 descriptor 一次推进约 2.1 秒时，顶层 RideOff 状态时钟仍只会
  按原生帧推进；
- 已知六张下级队列没有在约 2 秒时通过 `qword_1884D2118` 发生可见的计数、key、
  时钟或标志切换；
- 已经证伪“缺失的动作解锁只是再调用一次该清理槽，令某张已知下级队列退栈”。

这组运行证据只排除了上述两个槽的重放解释，不把尚未定位的动作所有权释放机制命名为
任何具体函数或字段。

## GraphAnimationManager 布尔事件观察

运行样本：
`artifacts/boarding/dismount_20260726_160834_062`。

`GraphAnimationManager` vtable slot 28 的既有 wrapper 先调用原函数，再在 RideOff
开始后的 3 秒内只记录 `contextIndex=0` 的首次原生 true；没有强制或改写返回值。
本轮记录到：

```text
elapsed  event
0ms      2, 73, 7, 10, 4
15ms     6, 1
```

15ms 以后直至 3 秒观察边界，没有另一个该上下文事件首次变为 true。与此同时顶层
RideOff 时钟正常经过约 100/300/700/1200/2000ms 里程碑，进程最终正常记录两条
`DLL_PROCESS_DETACH`。

因此已证伪“原生约 2 秒动作释放表现为 `GraphAnimationManager` slot 28、
`contextIndex=0` 的某个布尔完成事件从 false 变为 true，快速下车只需像快速上车一样
提前放行该事件”。这不把其他事件发布接口或非零 context 自行解释为已排除对象。

## 清理槽返回值没有下游用途

运行日志中，顶层 `base+0x5730` 的 `qword_1884D2118` 返回非零，而六张已知下级队列
返回零。Stage2 定点汇编已经排除“这个非零值是后续 active action 句柄”的解释：

```text
0x1831D7FD4  mov rcx, [arg_69B0]   ; base+0x5730
0x1831D7FDC  call qword_1884D2118
0x1831D7FE2  mov rcx, [arg_6AC8]   ; 立即覆盖 RCX，未读取 RAX
0x1831D7FEA  call qword_1884D2118
```

该区域是一长串相同的逐队列收尾调用；每次返回后都直接装入下一张队列参数，没有保存、
比较或转发前一调用的 `RAX`。因此顶层非零返回值在此调用链中被明确丢弃，不能作为快速
下车动作释放入口。

## 完整同步通道合并不会覆盖同帧目标区间

运行样本：
`artifacts/boarding/dismount_20260726_162700_154`。

本轮重新启用已经验证能生成原生末端落地的严格越界 evaluator，并只在 RideOff
子图六个精确 `ActionGraphResult_MergeChannelsByMask` 调用点，把实际路径原有的
mask `0x73` 扩展为 `0x7F`。这只增加 `0x04` 同步区间和 `0x08` sync-map 两个
通道，没有修改状态 key、状态队列时钟、世界坐标、delta 或求值次数。

运行时实际命中四级路径
`0x3670ACC -> 0x3671095 -> 0x36740C9 -> 0x36742D8`。第一层日志已经给出决定性结果：

```text
source = 0..2.1021, reachedEnd=1
before = 0..0.00834168
after  = 0..0.00834168, reachedEnd=1
```

来源和目标的 sync-map 指针相同；调用完整 `0x7F` 合并后，目标区间仍完全不变。
后续三层收到的来源因此已经是旧的 `0..0.00834168`，最终交给动态表
post-evaluate 的结果仍只携带单帧区间。静态
`ActionGraphResult_PropagateSyncFrame` 也明确允许在帧标识未发生所需变化时不复制
区间；mask 包含 `0x04` 并不等于无条件覆盖时间状态。

由此证伪“只要把四级 `0x73` 改成 `0x7F`，叶末端同步区间就会沿现有选择链自然到达
RideOff 外层结果”。该候选没有修复已确认的叶／外层时间分层，不能作为动作解锁方案。
`FLOW_PASS` 仍只代表测试流程与原生退出完成。

## DS2 状态机 event-space 的精确输入

DS2 `SetCurrentStateEventSpaceTimeInSMContext`（`0x1421BC7C0`）的定点汇编把此前
“读取当前项结果时间”的宽泛描述收窄到了两个精确字段。函数用队列计数和步长
`0xB8` 取得最新项，然后读取：

```text
latestEntry+0x50 = latestEntry.result+0x18 sync-map
latestEntry+0x84 = latestEntry.result+0x4C evaluationEndSeconds
```

它以 `basePhase=0` 把这个 `evaluationEndSeconds` 经 sync-map 转换成 segment phase，并把
返回的 float 写入 `queue+0x10`。函数不读取最新项 `+0xA0` 状态激活时钟，也不直接
读取 result 的 `timeState+0x10/+0x18` double 区间。

`ActionGraph_EvaluateDescriptorToResult`（`0x14219A7F0`）在
`0x14219A93F` 和 `0x14219AFB5` 精确写出这两个相邻字段：

```text
result+0x48 = descriptorDuration / timeScale
result+0x4C =
  (完整周期数 * descriptorDuration + 当前周期内的 clamped intervalEnd)
  / timeScale
```

非循环 RideOff 的完整周期数恒为零。普通帧中，`+0x4C` 是当前累计到的动作末端秒数；
严格越过 descriptor 末端时，evaluator 把周期内末端封顶到 descriptor duration，
所以 `+0x4C == +0x48`。因此此前暂称的 `syncDuration` 实际是
`evaluationEndSeconds`，不是静态 descriptor 时长，也不是 result timeState 保存的
double 起止区间。

必须把这个标量与失败样本中记录的 double 区间分开。DS2
`ActionGraphResult_MergeChannelsByMask`（`0x14018C640`）确认 mask bit `0x20`
会无条件复制来源 result 的 `+0x48`、`+0x4C`、`+0x50` 等元数据；RideOff 原生
四级 mask `0x73` 本来就包含 `0x20`。样本日志里的
`source=0..2.1021 / after=0..0.00834168` 只记录了 timeState double 区间，
没有记录 `+0x4C`。所以它只能证明 bit `0x04` 的同帧区间传播没有覆盖目标，不能证明
`evaluationEndSeconds` 没有到达外层。此前把该日志解释成“外层 `+0x4C` 仍为
`0.00834168`”是不成立的，现已撤回。

现有静态链反而确认：每一级 `0x73` 都会把严格越界叶的
`evaluationEndSeconds=2.1021` 继续复制到下一层。接下来必须查
`queue+0x10` 的实际事件／动作所有权消费者，不能再把“直接复制 `+0x4C`”当作待实现
修复。当前仍不把 `queue+0x10` 命名为玩家控制锁的唯一释放条件。

同一运行样本的顶层队列收尾日志已经提供动态交叉验证。严格越界后的第一帧即为：

```text
beforeClock=0.00834168
beforeDerived=2.1021
beforeMapped=2
beforeKey=0x0184F189
```

随后在 `beforeClock=0.1001 / 0.3003 / 0.7007 / 1.20954 / 2.01034` 时，
`beforeDerived=2.1021`、`beforeMapped=2` 和 RideOff key 都保持不变。也就是说，
末端 evaluation 标量及其映射值确实已到达当前队列项，但没有使当前 RideOff action
退役。该样本直接证伪“冻结只是因为 `+0x4C`／event-space 没有传播到外层”。
