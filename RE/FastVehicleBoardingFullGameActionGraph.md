# 玩家上车 FullGame ActionGraph 运行时分析

## 范围

本文记录 2026-07-11 对当前 `fullgame.dll` 玩家上车图的定点 IDA MCP
分析与 `test_boarding.ps1` 自动化运行结果。所有新增 hook 均为只读观测，目标由
唯一字节模式解析，没有使用固定 RVA 构建 hook，也没有修改寄存器、汇编或代码字节。

初始运行分析的测试路径固定为车辆左前方；2026-07-18 又增加了右前方、正前方运行
验证和三类 approach 选择树的定点静态分析。

## 活跃图入口

从 `ActionParams_QueryBoolEventByParamId` 的运行时参数反查到当前图实例：

```text
params+0x8A8 -> manager
manager+0xB8 -> graph instance
instance+0x20 -> descriptor owner
descriptor table first entry +0xD8 -> evaluator
```

自动测试解析出的 evaluator 位于 `fullgame.dll`，RVA 为 `0x315491C`。当前
IDA 地址为：

| 地址 | 当前命名 | 已验证角色 |
|---|---|---|
| `0x18315491C` | `PlayerActionGraph_EvaluateFrame_Stage1` | 巨型直线输入整理阶段，末尾调用 Stage2。 |
| `0x18318AC8D` | `PlayerActionGraph_EvaluateFrame_Stage2` | 执行 ActionGraph 分支和生成子图。 |
| `0x183566192` | `PlayerActionGraph_Subgraph_590480706_Evaluate` | 只在父图值等于 `590480706` 且两个状态字节有效时调用的子图。 |

Stage1 约有 29441 条指令且只有一个基本块；它只调用栈探测和 Stage2。这种
形态表明它主要是生成代码的参数整理区，不是适合直接 hook 的语义节点。

## ActionGraphResult 同步通道

`0x1801D6676` 已命名为 `ActionGraphResult_PropagateSyncFrame`。反编译确认：

- `inputResult+0x8` / `outputResult+0x8` 指向同步状态；
- 同步状态 `+0x8` 是帧号；
- 同步状态 `+0xC` 是瞬时标志；
- 同步状态 `+0x10` / `+0x18` 是本帧区间端点；
- `result+0x50` 是可选的同步区间重基准覆盖值；
- 传入的 `scale` 用于传播帧区间。

`0x1801D6834` 已命名为 `ActionGraphResult_MergeChannelsByMask`。它按掩码复制
ActionGraphResult 的多个独立通道：bit `0x04` 调用同步区间传播函数，bit `0x08`
复制 sync-map 指针，bit `0x20` 则单独复制 `timeState+0x0E reachedEnd` 和一组
result 元数据。同步区间传播函数本身不复制 `reachedEnd`。

`ActionGraphResult_PropagateSyncFrame` 对 `result+0x50` 的消费已经精确闭合：只有来源
值非负且目标 `result+0x54` gate 有效时才把它复制到目标；遇到 frame generation
不连续或已有覆盖值时，以 `max(result+0x50, 0)` 作为新区间起点，连续帧且该值为负
才沿用上一帧终点。因此 `+0x50` 是同步区间重基准覆盖，不是 descriptor duration、
`evaluationEndSeconds(+0x4C)` 或空间位移。

左前方上车运行时的同步传播全部使用 `scale=1.0`。玩家子图的典型区间为：

```text
顶层 Stage2：6.25... -> 6.24...，差值约为一帧
子图 590480706：0.00834168 -> 0
```

完成阶段首次出现的新调用点仍然只传播同样的一帧区间。输入和输出结果的
`duration`、`syncDuration` 均为 `0`。因此这些调用点不是约 3.2 秒上车动作的
播放头或动画长度所有者。

## 子图阶段差异

`PlayerActionGraph_Subgraph_590480706_Evaluate` 的同步调用可分为两组：

| 首次命中时机 | caller RVA |
|---|---|
| 上车早期，`elapsed≈0.025s` | `0x3566CCA`, `0x3568999`, `0x3568AF6`, `0x3568DF2` |
| 原生完成附近，`elapsed≈3.566s` | `0x3566FEA`, `0x3567E17`, `0x3567E64`, `0x356823F`, `0x35684A3`, `0x356A4F0`, `0x356A548` |

`0x18356A446` 是一个生成的多分支同步传播 helper。它会在候选结果间选择
同步距离较小的一项并继续传播，运行时只在完成阶段出现。它不持有动画资源，
也没有非零的动画时长字段。

## PlayspeedModifier 排除结果

`0x183693F8A` 通过其错误字符串和反编译逻辑确认是
`ActionGraph_PlayspeedModifier_Evaluate`：当 multiplier 非零时，它会用倒数缩放
输出 duration 和 sync-track 信息。

该函数的模式 hook 安装成功，但完整左前方上车过程中没有任何命中。因此这个
具体 PlayspeedModifier 分支没有参与当前上车动作，不能作为当前快上车介入点。

## 顶层输出通道

在每次 `0xED` 完成事件查询时，从 graph instance 的 `+0xC0` 取得顶层
`argOutput`，以 0.25 秒间隔读取非同步通道。整个上车过程均为：

```text
resultCount=0
resultItems=0
resultAltItems=0
resultSingle=0
```

这说明 `0xED` 查询对应的活跃图在该观察点没有输出可直接识别的姿势对象或动画
命令。这个观察只适用于事件查询发生时的顶层 `argOutput`，不能代表中间节点
结果为空。

## 完整结果通道中的 3.55355 秒节点

对 `ActionGraphResult_MergeChannelsByMask` 增加只读模式 hook 后，左前方上车在
`elapsed=0.025025s` 已出现以下中间结果：

```text
channelMask=0x73
resultCount=1
resultSingle=<非空引用计数对象>
duration=3.55355
syncDuration=0.00834168
```

该 `duration` 与原生完成事件约 `3.570s` 的时机高度吻合。它沿以下 caller RVA
传播：

```text
0x3607FCA
0x3608BDA
0x3609540
0x3609AF4
```

四个 caller 均属于 `0x183605C47` 开始的同一个生成子图函数。反汇编确认，这些
调用本身仍是候选结果选择与复制：先比较 2 或 3 个候选的同步距离，再以 mask
`0x73` 复制 count/items、alternate items、duration/结束元数据和 payload，但不复制
来源同步区间或 sync-map；它不是“完整时间状态”复制。

候选结果来自两个图状态槽：

```text
r14+0x50CC8
r14+0x50C68
```

两个槽均由相同的外部 evaluator 入口生成：

```text
qword_1884D1FF8(outputResult, descriptor, 0, xmm9)
```

对应 call site 为 `0x183607111` 与 `0x183607AB0`，descriptor 分别来自父对象
`+0x2730` 与 `+0x2710`。因此 `qword_1884D1FF8` 与这两个 descriptor 构成当前
已验证的动画结果生成边界；后续 merge 只选择和保留其结果。

这些生成结果在外层动态表中还会经过一个独立宿主边界。fullgame 的通用动态表求值器
在完成各个步长 `0xB8` 的条目后调用运行时槽
`ActionGraphDynamicTable_PostEvaluate_Ptr`；初始化器以哈希 `0x49589132` 解析该槽。
三个调用点确认其 ABI 为
`PostEvaluate(dynamicTable, deltaSeconds, outputResult)`；其中一个通用 helper
会在该槽返回后立即把其临时 ActionGraphResult 以 `0x73` 向上合并，另一个随后才清理
动态表状态。它的实现不在 fullgame 文件中，所以叶 evaluator 或 `0x73` merge 的日志
不能代替该宿主边界的输入/输出结论。

运行时 scratch 地址与静态槽位初始化的交叉计算确认，左前方路径实际选中的是：

```text
r14+0x50CC8 -> descriptor [rbp+0x2730] -> duration 3.55355
```

相邻的 `r14+0x50C68` 槽以及另一侧相邻槽在同一帧均为全空结果：count、single、
duration、syncDuration 全部为零。当前路径中不存在一个已经求值完成、可以直接
替换长结果的相邻短动画或坐姿候选。

`qword_1884D1FF8` 的完整 ABI 有五个参数。第 4 参数在该子图中来自全局常量
`0x3F800000`，即 `1.0`；DS2 核心反编译确认它是 descriptor `timeScale`，会缩放
采样区间，并把输出 duration 写为资源 duration 除以 timeScale。第 5 参数是 bool，
控制额外姿态/结果通道求值；生成代码固定传入 `1`。

长结果所在分支读取一个动态表：表首为条目数，`+0x8` 为条目数组，单条大小
`0xB8`，条目 `+0x30` 为哈希。当前结果只在条目哈希 `0x0BC4A758` 且活动标志
有效时生成。这是高层图状态分支，不是动画时长字段本身。

## 三类 approach 的结果选择树

DS2 `DSPlayerVehicleRideOnState_ProcessVehicleAttach` 把 approach class 转为 float，
写入玩家 `+0x3890` 的三值结构。fullgame 生成图中的 `[r14+0x507CC]` 随后以
`0.0/1.0/2.0` 进入三候选选择器；定点反汇编闭合出的 side 0 路径为：

| approach | 生成图结果 | descriptor evaluator |
|---:|---|---:|
| `0` | `var_4D0` | `0x183607111`，return RVA `0x3607117` |
| `1` | `var_4C8` | `0x183607B62`，return RVA `0x3607B68` |
| `2` | `var_568` composite 候选 A | `0x18360794C`，return RVA `0x3607952` |
| `2` | `var_568` composite 候选 C | `0x183607C14`，return RVA `0x3607C1A` |

approach 2 的两个 evaluator 结果在 `0x18360844F` 进入原生
`SelectNearestOfTwoAndRetain`，不是可以静态删掉其中一项的重复调用。四个调用点和
另一个独立动态表调用点均静态引用同一 evaluator 指针槽 `0x1884D1FF8`。
`0x183609E08` 使用 descriptor pack `+0x3478`，属于另一套动态表，不是第三个
approach leaf。

右前方运行由用户固定测试存档位置后，日志把 action hash `0x6F53F3A5`、
approach 1 与 return RVA `0x3607B68` 对位。原生求值结果为：

```text
duration=3.01968 sync=0.0166834 end=0
```

同一 leaf 使用 `timeScale=512` 后为：

```text
duration=0.00589782 sync=0.00589782 end=1 complete=1
```

这是较早的 `timeScale=512` 运行。该版本随后先结束 CutIn、再放行内部事件 186，
Drive Enter 发生在 RideOn elapsed `0.0792459s`。根 `test_boarding.ps1` 完成上车、
Drive、原生下车、退出与正常 DLL 卸载，没有 CrashTrace。

正前方运行由用户固定测试位置后，实际命中：

```text
leaf=8 callerRva=0x3607C1A
actionHash=0x3897A3D5 variant=0
```

这把正前方与 approach 2 composite 候选 C 直接对位。当前多帧 evaluator wrapper 使用
`timeScale=64`，原函数在数个正常求值帧后给出：

```text
duration=0.0550029 sync=0.0550029 end=1 complete=1
```

该结果保留正常非空 single、sync state 和附加姿态通道；wrapper 没有直接写
`reachedEnd`、sync frame 或结果对象。

`resultSingle` 每帧由不同的临时地址承载，但对象中的第一个指针和第二个稳定引用
保持不变。其余采样字段主要表现为权重/变换值变化，没有观察到简单的线性累计
播放头。这与“每帧生成姿势结果对象、资源/上下文引用保持稳定”的模型一致。

## Seat Controller type-6 槽位

同一运行中，seat controller 的四个 type-6 槽位从上车开始到完成始终为：

```text
handle=0xFFFFFFFFFFFFFFFF
type=6
```

`mode` 在约 `3.257s` 从 `0` 变为 `1`，完成后变为 `2`；`value78` 保持为极小
哨兵值。type-6 槽位没有保存可见长动画的资源句柄或播放头。

## 已验证边界

当前已排除以下对象作为长上车动画时间线所有者：

1. `ActionGraphResult_PropagateSyncFrame` 的单帧同步区间；
2. 当前未命中的特定 `PlayspeedModifier` 分支；
3. `0xED` 查询时为空的顶层结果对象通道；
4. 始终为无效句柄的 seat controller type-6 槽位；
5. `0x183605C47` 内只负责选择和复制候选的 merge call sites。

新的完整通道证据修正了此前只看同步 helper 得出的边界：ActionGraph 中间结果
确实携带与上车时长吻合的 `3.55355s` duration 和非空动画结果对象。当前最窄的
已验证生成边界是 `qword_1884D1FF8` 对两个 descriptor 的求值；同步 helper 与
后续 merge 都不是时长产生者。

## 自动化验证

本轮代码变化均先运行 `ds2_vehicle_boarding_trace/build.ps1`，再运行
`test_boarding.ps1`。新增 result-channel hook 与对象采样也完整通过：

- fullgame 只读 hook 安装成功；
- 左前方上车进入 `DriveEnter`；
- 下车完成；
- 游戏按脚本正常退出；
- 没有崩溃报告。

## 2026-07-11 空结果替换崩溃

一次功能性实验在提前进入 `DriveEnter` 后，把仍在传播的 `3.55355s` 完整结果
替换成相邻的空 scratch result。替换发生在 `elapsed=0.0417084s`，随后同帧在
`0x183607FEA` 触发 `0xC0000005`。

异常点属于 `0x183605C47`，位于 `ActionGraphResult_MergeChannelsByMask` 返回后的
引用传播逻辑：

```text
movzx eax, byte ptr [r14+0x50BA0]
mov   rcx, [r14+0x50B88]
test  rcx, rcx
jz    fallback
...
fallback:
mov   rcx, [r14+0x50B48]
movsxd rcx, dword ptr [rcx]   ; rcx == nullptr，异常
```

空 scratch 虽然自身的 count、single、duration 均为零，但 mask `0x73` 的局部
合并没有把输出对象的全部计数/引用通道原子地清空。输出仍保留非零计数语义，
对应引用指针却为空，所以下游引用计数传播必然解引用空指针。

这次崩溃把边界进一步钉死：不能用空结果表示“无动画”，也不能跨帧缓存
`resultSingle`。合法替换只能来自同帧由原 evaluator 生成、并携带完整 count、
single/items、duration、sync 与引用所有权的 seated/Drive result。当前构建因此
保留完整长结果，并只读观察提前 Drive 后是否自然产生这种有效结果。

## 2026-07-11 descriptor 模式实验

通过 `0x0BC4A758` 分支的完整唯一签名定位间接 evaluator 槽，并只对该分支把
第三参数从 `mode=0` 改为 `mode=1`。原 evaluator 仍生成完整结果，自动测试的上车、
Drive、下车与退出均通过，但三张窗口截图显示：约 `0.2s` 角色仍在车侧起身，
约 `0.7s` 正在攀爬，约 `1.7s` 已站到车顶。长动作没有被跳过。

结果 duration 仍为 `3.55355s`，只有 syncDuration 从常见的单帧区间变成约
`0.025s`。因此第三参数只影响 descriptor 的同步/求值模式，不是“立即完成”或
“选择坐姿”的开关。该模式不作为实现方案保留。

## 2026-07-11 descriptor 零 timeScale 实验

只对同一个上车 descriptor 把第四参数从 `1.0` 改为 `0.0`。状态机、下车和退出
流程仍通过，但 evaluator 输出的 duration 不再有效，截图中角色在约 `0.7s` 后
消失，驾驶位保持为空；这表示上车 pose 层被降为无贡献，并没有显露一个已坐定的
底层 pose。后续核心反编译确认该参数是 timeScale，不是权重；零值会破坏原生时长
计算，因此该实验不构成有效的“无动画”语义。

同期日志确认 `PlayerActionGraph_Subgraph_590480706_Evaluate` 内有一条连续同步帧
传播链，四个 caller 依次把前一个输出作为下一个输入。第一处 caller 的输入区间为
单帧 `0.00834168s`，后三处只继续传播。因此如果第三参数是时间比例，应该只在
第一处放大一次，不能在四处重复相乘。

## 2026-07-11 首帧同步终点实验

该历史实验使用 `ActionGraphResult_PropagateSyncFrame`，但不是全局改速：

1. 先用 `0x0BC4A758` 上车分支及其四层局部传播 call site 的完整唯一签名定位；
2. 只在玩家原始 OnEnter、attach stage 2、seat pose/filter 和原生 bit24 门均完成后启用；
3. 读取 sync state 的原生 frame id；
4. 只在首个活动 frame 对该四层传播传入 `scale=512.0`；
5. 后续 frame 立即恢复原生 `scale=1.0`。

首层放大后，原 evaluator 仍输出完整 `count=1`、非空 `resultSingle` 和合法引用，
`duration` 保持 `3.55355s`，`syncDuration` 被原函数钳制到 `3.55355s`。这表示同一
原生 clip 在一个求值帧内跨到末端，而不是伪造 duration、清结果或改播放头内存。

自动截图形成明确对照：

- 未加速：约 `0.2s` 在车侧、`0.7s` 攀爬、`1.7s` 站在车顶；
- 首帧同步终点：不再出现上述攀爬序列，约 `0.7s` 已在座位位置收敛，`1.7s`
  稳定坐定。

同一次 `test_boarding.ps1` 运行完整通过 Drive、四秒车内停留、首次下车和退出，
没有 CrashTrace。四层全部放大与只放大首层在画面上相同；该实验当时只保留首帧门，
避免在五秒功能窗口内反复放大。该历史实验没有闭合独立的 `DSCutInCamera`
生命周期，因此当时不能视为最终快速上车实现。

## 2026-07-18 evaluator timeScale 实现

当前功能构建不再 hook `ActionGraphResult_PropagateSyncFrame`。它在两个唯一模式解析并
校验出的同一 evaluator 可写函数指针槽安装五参数 C++ wrapper。四个 approach leaf
调用点必须各自唯一，且都必须解析到同一槽；另一个 `+0x3478` 动态表调用点只用于
交叉校验，不作为活动 leaf。

wrapper 将同一会话第一次实际命中的 `(leaf, descriptor)` 绑定为活动结果，并在每次
合法求值时把第 4 参数从 `1.0` 改为 `64.0`，第 5 参数原样透传。它持续调用原函数，
直到原结果满足 `reachedEnd` 或 `syncDuration >= duration`，而不是只修改首帧后就认为
descriptor 已完成。

正前方原 evaluator 最终结果为 `duration=0.0550029`、
`syncDuration=0.0550029`、`reachedEnd=1`。RideOn 随后通过原生 event 186 完成并进入
Drive；Drive 后的玩家动画姿态提交完成后，CutIn 才走原生 finished、CameraMode
Deactivate 和驾驶镜头 handoff。自动测试最早 `200ms` 截图已是正常车辆后方镜头。

后续按 Drive 结果链重新定位并结合原生时序确认，`6.45645s` 完整结果在
`Drive Enter` 后立即开始，属于 Drive descriptor，不是 RideOff。旧的 RideOff
归因已被推翻。
