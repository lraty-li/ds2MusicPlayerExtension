# DS2 玩家快速上下车：当前状态与知识索引

日期：2026-08-23

> **当前结论：**快速上车已经验证；当前卡车驾驶位样本的快速下车也已通过原生退出、
> Basic 动作接管、Fall 缺席和移动输入四项验证。实现逐次推进 RideOff 长 descriptor，
> 同步推进其实际外层动态表时钟，但在 descriptor 终点前 `0.1s` 停止注入额外时间，
> 将最后几帧和动作退出交还原生生命周期。它不再强制 `pending 3→0`、不在 mover
> 消费终点姿态前 detach，也不强制产生 `reachedEnd=1`。自动样本在约 `1.094s`
> 连续记录 RideOff OnExit、RideVehicle OnExit 与 Basic OnEnter，之后按住 S 可立即
> 移动且没有进入 Fall。历史 `PASS`/`FLOW_PASS`、单帧末端和强制 detach 候选仍是
> 失败证据，不得与当前实现混用。

## 当前部署状态

本地构建与游戏目录中的当前 ASI 已重新核对一致：

```text
size    = 343552
SHA-256 = 56D19BE0DAFE7898BA2DA82AEF1575AE71A8677775502ABBC3FC42FDAF8E12E0
```

当前实现保留原生 RideOff 动作与根运动，只对精确长 descriptor 每次最多增加
`0.25s`，并把相同额外量送入运行时实际命中的外层
`ActionGraphDynamicTable_PostEvaluate`。最后 `0.1s` 不再加速，避免在原生
RideVehicle 退出前先制造叶 `reachedEnd=1`。样本
`artifacts/boarding/fast_dismount_20260823_231008_715` 返回
`FAST_DISMOUNT_TRACE_OK`；本地与游戏目录哈希均为上值。该结论只覆盖已经运行验证的
卡车驾驶位与当前存档，其他车辆/座位变体仍需各自运行验证，不能外推为全部载具已覆盖。

以下构建均为历史诊断或失败候选，不代表当前部署状态。

已经证伪的完整区间候选有两个连续构建：
`57D8BEE6257F974CB211F86CB267515FADA661AB3A8E0C39A7F2F59F949892C8`
（333824 字节）完成自动流程与视觉关键帧，随后
`0C135ACBE3973BD58F3AB2D2392D92C64DA5FDD0136D8A3FCCAC70061A745D05`
（333312 字节）只删除误导诊断读取。用户实际操控确认后者会完全锁死玩家动作。该轮
回退曾从源码与工程删除 `RideOffGraphEndpoint`，也没有恢复会让角色站到车上的
`RideOffStateBypass`；这是历史状态。当前版本重新引入的是同步渐进推进实现，不是该
一次性完整区间候选。

随后用于验证严格越界条件的构建为 334336 字节，SHA-256
`9418291192221BA395F3EAB1518C0C6C5C13598B92B61C5DDD69AE06FACB1432`。
它由原 evaluator 正常产生 `end=1`，但落地后的 W 探针仍无响应，因此同样已经证伪。
该失败构建已经撤回，不得继续部署。

同步通道合并候选为 348160 字节，SHA-256
`42644E2D90E36B048D7CEF22D93EB39ED21FA15D5590E633E4BA60A78F1F0273`。
样本 `dismount_20260726_162700_154` 确认它命中实际四级 RideOff 路径，但首级
`0x7F` 合并没有改变目标的单帧同步区间，后续层也只能继续传播旧区间。该候选已经
判定失败；相关同步通道改写代码随后删除并重新 `BUILD_OK`。该失败 ASI 不再部署；
当前游戏目录产物以本页顶部的 343552 字节哈希为准。详细运行证据见
[FastVehicleRideOffStateQueueRuntime.md](FastVehicleRideOffStateQueueRuntime.md)。

## 快速上车：已完成

`ds2_vehicle_boarding_trace` 保留原生 RideOn 状态语义并压缩展示时间：

1. 原生 RideOn OnEnter、玩家 seat transition、实体挂接和 stage `0 -> 1 -> 2`
   正常执行。
2. 正前方 `DSVehicleTruck` 的机械上车 request `3/4` 只在 RTTI、会话、current 与
   controller playback 完成态全部匹配时，于车辆消费前取消；驾驶座保持基线状态 `0`。
3. 活动 fullgame descriptor 由原 evaluator 以 `timeScale=64` 跨帧推进，直到原生
   `reachedEnd` 或 `syncDuration >= duration`，不伪造完成结构。
4. 原生 ActionGraph 完成事件进入 RideOn 公共完成块，并正常请求 Drive。
5. Drive 后等待 `DSPlayerMoverAccessor::OnModifyAnimatedPose` 提交驾驶 world basis，
   再让原生 `DSCutInCamera` playback 完成并由原生 Deactivate 交接镜头。

用户已确认左前、右前和正前三个方向均可直接进入座位。按键同源时钟的精确上车序列中，
角色约 `117ms` 仍在车外、约 `172ms` 已坐入车内，没有观察到攀爬中间动作。正前方
路径命中 `approach=2`、leaf `8`、caller RVA `0x3607C1A`、action hash
`0x3897A3D5`，并消除了旧版本的垂直看地镜头回归。

快速上车各层的详细静态分析见：

- [FastVehicleBoardingTruckMechanicalAnimation.md](FastVehicleBoardingTruckMechanicalAnimation.md)
- [FastVehicleBoardingFullGameActionGraph.md](FastVehicleBoardingFullGameActionGraph.md)
- [FastVehicleBoardingCompletionEvent.md](FastVehicleBoardingCompletionEvent.md)
- [FastVehicleBoardingCutInCameraLifecycle.md](FastVehicleBoardingCutInCameraLifecycle.md)
- [FastVehicleBoardingAnimationSystem.md](FastVehicleBoardingAnimationSystem.md)

## 快速下车：当前卡车路径已通过功能验证

当前实现的关键边界不是“强制到末端”，而是“加速到末端前并交回原生”：

1. 精确 RideOff descriptor 每次最多推进 `0.25s`；
2. 同帧额外量同步到实际外层 post-evaluate 调用
   `fullgame+0x360CCEB`（返回 RVA `0x360CCF1`）；
3. 加速上限为 `duration - 0.1s`，最后几帧只使用原生 delta；
4. 不写车辆子状态 `pending=0`，不调用强制 detach，不人工调用 Basic OnEnter；
5. 只接受原生 RideOff/RideVehicle OnExit 后立即进入 Basic，且控制窗口无 Fall。

验证样本 `fast_dismount_20260823_231008_715` 在 `elapsedMs=1094` 同一时刻记录：

```text
callerRva=0xF97B56   RideOff OnExit
callerRva=0x1004F88  RideVehicleActionPlugin OnExit
callerRva=0xFB40B6   BasicActionPlugin OnEnter
```

三次调用的 mover 坐标一致，Z 为 `102.2153168`；日志中
`callerRva=0x110694D` 的 Fall OnEnter 次数为零。S 控制截图在实际 `148ms`
已经出现转身/迈步，实际 `429ms` 已离开按键前位置。由此同时闭合动作接管、稳定落地与
输入恢复，不能再沿用“视觉通过但功能失败”的旧结论。

### 历史失败候选

有效视觉基线为 `artifacts/boarding/dismount_20260725_215713_944`。以
`capture_manifest.csv` 的实际捕获中点为准：

```text
约 70ms       腿部仍在车辆内
约 1.508s     仍处于下车动作
约 2.016s     才完整站到车外
```

当时的诊断基线在第一次原生 RideOff RunPresentation 后调用原生终结器，并只在
Presentation TLS 内让动画 ready 查询通过。这会快速完成 RideOff、CutIn 和 Free
状态；旧版本不会取消已经建立的玩家下车动作。

已经确认的两个非时间快进绕过也都失败：

- 在 RideOff OnEnter 内抑制 `state=4` 和 `request=1`，再调用 operation 21 detach、
  原生终结器并切 Free：流程完成，但 320ms 仍有屈腿下车姿态。
- 在 Drive 提交 RideOff 前直接 operation 21 detach 并把 pending 改为 Free：动作确实
  消失，但角色立刻站到车辆上方，属于错误落点。

已证伪的第一版完整区间实现没有绕过 RideOff 动作入口，也没有改变 evaluator 的
`timeScale`：

1. 只在活动 RideOff 5 秒会话内匹配 fullgame 精确返回点 RVA `0x366F423`、`mode=0`、
   `timeScale=1`、`evaluateExtraChannels=1` 的长 descriptor；
2. 只接受 `timeState+0x0C=0` 的原始秒数区间，保留 `start(+0x18)`，把
   `end(+0x10)` 及其 float 镜像提交到 `descriptor+0x3C` duration；
3. 只调用一次原 evaluator。验证日志为
   `interval=0..0.00834168->2.1021`，随后
   `duration=2.1021 sync=2.1021 complete=1`；
4. evaluator 生成的末端姿态与剩余 motion transform 由原生 pose context 写入
   SkinnedModel 当前双缓冲，再由普通 mover/物理链消费。

该版视觉样本为
`artifacts/boarding/dismount_20260726_115305_099`。按
`capture_manifest.csv` 实际中点，117ms 已完全离开座位并位于车旁落地轨迹；
320ms、726ms 均未再出现车辆下车姿态，也没有站到车体上；这些静态帧本身不能证明
玩家控制已经恢复。只查看了这三张首选关键帧，没有为得出视觉结论追加查看
1200/2000ms。

同一轮游戏日志在 `11:53:13.000` 同时记录 VehicleBoard 模块与 `DllMain` 的
`DLL_PROCESS_DETACH`，确认测试通过正常进程退出完成，并非脚本强杀造成的流程假阳性。

用户随后进行实际操控测试，确认虽然下车视觉确实瞬间完成，但玩家动作完全卡住，无法
执行任何动作。这一结果推翻了“快速下车已完成”的结论：117/320/726ms 关键帧只能
证明视觉动作和落点，不能证明 ActionGraph/玩家动作状态已恢复。Graph 末端区间候选
因此判定失败，必须撤回；今后的成功标准必须同时包含落地后的移动和普通动作输入。

DS2 evaluator 的定点汇编已经解释了这次失败与日志为何同时出现。`mode=0` 路径在
`0x14219A9F1` 比较请求终点与 descriptor duration；终点小于或等于 duration 都走
`JBE`，只有严格大于 duration 才在 `0x14219A9F8` 钳制到末端，并于
`0x14219AA01` 写 `timeState+0x0E reachedEnd=1`。旧实现写入的正是
`end=duration=2.1021`，所以日志得到末端 `sync=2.1021`，但原生 `end=0`。这证明
该候选当时属于“生成了末端结果但没有声明原生结束”；后续严格越界实验又证明，即使
原生 `end=1`，一次性末端推进仍不能安全释放动作。

严格越界候选随后使用 duration 的下一个可表示 float 作为请求终点，让原 evaluator
自行钳制并写完成位。自动样本
`artifacts/boarding/dismount_20260726_122349_721` 确认：

```text
duration=2.1021 sync=2.1021 end=1 complete=1
```

视觉仍在 133ms 离座、320ms 落地、718ms 无约 2 秒下车动作；但测试在
800–1700ms 自动按住 W 后，1218ms 和 2031ms 的玩家位置、倾斜姿态仍与 718ms
一致。由此确认原生 `reachedEnd=1` 仍不足以释放当前玩家动作锁。`FLOW_PASS`、正确
落点和原生完成位三者同时成立，仍不能证明快速下车可用。

fullgame 结果传播进一步确认：RideOff 长结果经两处 mask `0x73` 合并时，会通过
`0x20` 通道复制 `reachedEnd`，但不会通过 `0x04` 传播该来源的同步区间，也不会通过
`0x08` 复制其 sync-map。因此“长结果返回 `end=1`”不等于最终输出采用了该长结果的
完整时间状态。这个已确认的通道拆分解释了为什么不能继续只盯完成位，但尚不足以单独
确定冻结根因。

此外，该叶结果经四级 `0x73` 选择后只是写入步长 `0xB8` 的动态表条目；fullgame
在已确认的 `0x0184F189` RideOff 条目分派之后，实际路径最终在公共出口
`0x18360CCEB` 调用宿主解析槽（哈希 `0x49589132`）进行动态表 post-evaluate。
运行时精确返回 RVA 为 `0x360CCF1`。旧标注 `0x183607896` 属于 key
`0x4404D873` 的局部表，RideOff 子图从 `0x183607217` 返回后不会落入该调用。因此现有
`end=1` 日志只闭合叶 descriptor，不闭合动态表输出或玩家顶层动作退栈；这比
“完成位没有写入”更准确地限定了当前缺口。

这张表的实例来源也已闭合：`PlayerActionGraph_EvaluateFrame_Stage1` 在
`0x183162E41` 从 Graph 运行时状态基址取 `base+0x5730`，随后依次作为 Stage2
`arg_69B0`、`sub_183594DF9` 的 `arg_5B8`、以及 `sub_183605C47` 的第 17 个
参数传入。Stage2 在 `0x18318C135` 对同一对象调用解析槽
`qword_1884D2108`，初始化／提交默认 key `0x78EC48BF`；相关槽由初始化器 hash
`0x6685E865` 解析。该 key 的具体玩法名称尚未闭合，不能提前命名为 Free。由此确认
`0x18360CCEB` 操作的是玩家 Graph 持久状态块中的当前动态表，而不是 wrapper
临时构造的叶结果。

同一 `arg_69B0` 还经四级明确的栈参数传递
`6468 -> 2300 -> 1A0 -> C8` 到达 `sub_18339A856`。该函数读取同一队列当前条目的
状态 key，并通过 `qword_1884D2100` 提交真正的状态间转移；在
`0x18339ABA8..0x18339ABB3`，参数已闭合为 exact queue、目标
`0x0184F189`、0x20 字节 `TransitionProperties` 和零附加参数，确认这是顶层进入
RideOff 的转移。通用 helper `sub_1801D1E2F` 的同形调用也证明
`qword_1884D2100` 是转移槽，不能再把默认提交槽 `qword_1884D2108` 当作运行时
转移入口。当前仍未确认 RideOff 转出的条件、目标 key 与属性，所以没有据此记录或
恢复任何实现候选。

进一步的当前状态分支复核确认：顶层 current key 已是 RideOff `0x0184F189` 时，
预处理块不会对 exact queue 再调用 `qword_1884D2100`，而是直接进入 RideOff 活动
条目的专用求值分支。该分支内后续可达的转移调用使用的是多张下级状态队列参数，不是
再次直接取顶层 `arg_C8`。这些下级参数现已逐级追溯到 Graph 基址固定偏移：
`base+0x5750`、`+0x5770`、`+0x5790`、`+0x58F0`、`+0x5910` 和
`+0x5930`，与顶层 `base+0x5730` 明确不同。对应转移槽 ABI 也已由通用 helper 闭合为
五参数并返回指针值，而不是三参数无返回调用。长 descriptor 的 work 基址现已沿
Stage1/Stage2 参数链精确追到 `base+0x58F0`；其两个求值 gate 是
`work+0x13` 和 `work+0x22`，后者已进入相邻 `base+0x5910` 块。这个对应关系仍未
命名两个原始字节的业务语义，也不证明某条已知下级转移负责动作释放；不能把
“强制顶层切回默认”或任意重放一条下级转移写成修复。

安全基线运行观察已经进一步证伪“漏掉下级转移”解释。透传包装
`qword_1884D2100` 后，本轮五秒 RideOff 会话只记录到一次调用：顶层队列从
`0x4404D873` 追加目标 `0x0184F189`，队列计数 `1 -> 2`、新项时钟归零；其返回点
与静态 `0x18339ABB3` 一致。原生约 2 秒动作完成前后没有任何第二次
`qword_1884D2100` 调用，日志预算未耗尽，进程也正常退出。因此可用快速下车不能靠
猜测并重放某条嵌套 target key，后续应定位不经过该槽的队列收尾或动作所有权释放。

只读清理槽观测又排除了更窄的“漏调一次队列清理”解释。样本
`artifacts/boarding/dismount_20260726_155902_781` 中，
`qword_1884D2118` 在七张已知队列上的每次已记录调用均不改变计数、最新 key、
`+0xA0/+0xA4/+0xA8` 或转移标志。六张下级队列没有在约 2 秒时出现这些字段变化；
只有顶层 `base+0x5730` 保持 RideOff key，并从 `0.00834168` 按原生帧累计到
`2.00617`。因此动作释放既没有重放转移槽，也不是简单重放该清理槽让某张已知下级
队列退栈。完整运行证据已拆分到
[FastVehicleRideOffStateQueueRuntime.md](FastVehicleRideOffStateQueueRuntime.md)。

这里的“七张已知队列”不是 Stage2 的完整队列集合。对帧尾基本块的精确有界枚举确认，
`0x1831D7F0A..0x1831DB6A6` 共连续调用 `qword_1884D2118` 1020 次，最后一次后直接
进入函数尾声。因此七张队列的无变化只排除了 RideOff 已知子图内的清理遗漏，不能
排除其余 1013 张队列中的上级或并行动作所有权队列。

早期样本 `dismount_20260726_160834_062` 只记录 slot 28 的首次 true，且没有越过
2.1021 秒自然边界，不能据此证伪既有事件在末端发生边沿变化。新样本
`dismount_20260726_184026_386` 已按 `(manager,event,context)` 记录 false 和边沿，
但 192 条预算由 20 条首个 true 与 172 条窗口心跳在 1829ms 恰好耗尽；此前没有
`reason=edge`，自然边界本身仍无事件数据。因此 slot 28 完成边沿目前既未确认也未
排除。

Stage2 收尾汇编还确认，顶层 `qword_1884D2118` 的非零返回值没有被消费：
`0x1831D7FDC` 调用后，`0x1831D7FE2` 立即装入下一张队列并继续调用同一槽，既不读取
也不保存 `RAX`。因此该返回指针不是遗漏的 active action 句柄。

该宿主槽还接收独立的 `deltaSeconds`。失败候选把叶区间一次推进到 `2.1021s` 时，
`0x18360CCEB` 曾向 RideOff 所在动态表的 post-evaluate 传入原始单帧增量。因此即时末端姿态与外层
动作表时间线发生了已确认的分层。
而且该槽的 `outputResult` 是外层 `entry+0x38`：它只从叶复制了 `reachedEnd` 与
内容/payload，仍保留条目自己的同步区间，并非 wrapper 改写过的叶 timeState。

DS2 宿主实现现已进一步闭合这处分层。动画导出注册表把两个槽分别解析到
`ActionGraph_StatesQueueUpdateTime`（`0x14219DE00`）和
`ActionGraph_StatesQueueEvalLogic`（`0x14219F880`）。后者在
`0x14219F8C7` 只用其独立 `deltaSeconds` 参数累加最新队列项 `+0xA0`；叶
`reachedEnd=1` 在内部 helper 中只改变终端时间的取模/封顶派生，不会删除队列项。
其最终 helper `ActionGraph_StatesQueue_MergeEventTagActivity` 只把各条目中的
event/tag 活动记录合并到输出 `result+0x38`，也不改变 count、state key 或时钟。
真正的旧项退役发生在 `StatesQueueUpdateTime`：它从输入结果的时间区间长度推进过渡
计时，满足条件后调用已确认的 `0xB8` 步长数组擦除函数 `0x1402389A0`。因此失败候选
同时完成叶末端姿态与根运动，却只让外层状态队列前进一个原生帧；视觉完成、叶完成位、
状态队列累计时间和旧项退役是四个不能互相替代的条件。通用宿主 post-evaluate 已确认
不隐藏当前 action 退役步骤，释放判断位于 fullgame 生成式 RideOff 逻辑。

`ActiveStatesQueue_PushActiveState` 的实现进一步确认，队列以 `entry+0x30` 保存状态
哈希；新状态追加或同一当前状态重激活都会把 `entry+0xA0` 清零，随后再由
`StatesQueueEvalLogic` 按帧累计。`ActiveStatesQueue_RemoveActiveStates(index)`
则精确删除队首到 index（含）的活动状态。因此 `+0xA0` 是独立的当前状态累计时钟，
但 RideOff 是否由它触发转出仍未确认，不能直接把写大该字段当成已成立的修复。
已反编译的 `SetCurrentStateEventSpaceTimeInSMContext`（`0x1421BC7C0`）读取当前项
结果的 `result+0x18` sync-map 和 `result+0x4C` evaluation-end 秒数，将后者映射后写入
`queue+0x10`，而不是读取 `+0xA0`；状态激活时钟、叶 timeState 与状态机事件时间
不能混为一谈。`ActionGraph_EvaluateDescriptorToResult` 已确认：
`result+0x48 = descriptorDuration / timeScale`，而 `result+0x4C` 是完整循环数和当前
周期内封顶末端共同计算出的当前动作末端秒数；非循环 RideOff 严格越界时两者才相等。
对该 evaluator 整个函数及其两个直接结果-item helper 的指令级复核还确认，它们都不
读写输出 `result+0x50/+0x54`；函数内出现的这两个位移只是栈局部矩阵。因此严格越界
不会在 descriptor evaluator 内直接产生顶层控制器所需的同步区间重基准覆盖值。
但 evaluator 并未跳过 descriptor 事件：它会把实际采样的 `0..duration` 交给
`AnimationDescriptor_CollectActiveEventTagsForInterval`，遍历
`descriptor+0x50` 事件表并收集所有与区间重叠的 tag payload。由此排除“只得到末端
姿态／根运动、完全没有执行叶事件遍历”这一冻结解释。
DS2 的结果合并实现同时确认 mask bit `0x20` 会无条件复制 `result+0x48/+0x4C`；
RideOff 原有的四级 `0x73` 已包含该位。因此严格越界叶的
`evaluationEndSeconds=2.1021` 会经过这四级选择，失败的 `0x7F` 实验只证明
timeState double 区间没有在同帧被覆盖。此前根据区间日志推断“外层 `+0x4C` 仍是
单帧值”属于错误解释，已经撤回；直接复制 `+0x4C` 不是新的修复方向。
运行日志也确认严格越界后的第一帧顶层项已经是
`clock=0.00834168, derived=2.1021, mapped=2`，之后直到 `clock=2.01034` 仍保持
末端派生值和 RideOff key。末端 event-space 数据已经建立但动作仍未退役，后续必须
定位当前 RideOff action 的完成事件或所有权释放链。fullgame 又确认长 descriptor
的 `work+0x13` gate 物理落在 `base+0x58F0` 下级队列的 `queue+0x10` 浮点最高字节，
另一 gate `work+0x22` 与相邻队列 count 的第 3 字节重叠；这证明队列头存储直接参与
该叶是否继续求值，但生成图存在临时复用，不能把这些字节直接命名为动作锁。

现有自然与严格末端样本的最后窗口分别只到约 `2015ms` 与 `2032ms`，均未跨过
`2102.1ms` 原生时长；当前布尔事件 observer 又不记录 false／重复 true 边沿。因此
此前日志没有真正覆盖自然完成后的第一帧，不能用“约 2 秒没有新事件”排除完成边沿。

fullgame 精确控制流已经排除 RideOff 叶子图内部存在这个释放消费者：从叶 evaluator
返回到第一层 `0x73` 选择合并的实际路径不读取叶 timeState、`reachedEnd` 或
`evaluationEndSeconds` 作退役判断；继续到该子图最终合并点也只有活动门、
动态表 post-evaluate 和结果选择。叶结果的末端标志会向外传播，但释放当前 action
的逻辑必定位于 `PlayerActionGraph_Subgraph_0184F189_Evaluate` 之外。

其直接父函数现已确认对同一个 RideOff key `0x0184F189` 维护三张独立动态表：
中间表进入已知 2.1021 秒姿态子图；另外两表分别在 `0x183606D6A` 和
`0x183609EF1` 求值 descriptorPack `+0x35A8`、`+0x3538`，两者都是
`mode=0/evaluatePose=0` 的非姿态结果。不过运行样本
`dismount_20260726_185846_956` 中，精确中间姿态层命中 70 次，这两个同-key
call site 均为零命中。它们不在本次实际 RideOff 路径活动，冻结不能解释为旧候选
漏推进这两条 descriptor，也不得把它们加入 Mod。

该样本还确认自然中间层越过 2.1021 秒后仍逐帧求值：输入区间终点继续增长到
2.4024 秒，输出则保持 `duration=sync=2.1021/end=1`。自然完成并不会停止
descriptor 调用或把输入播放头冻结在 duration。

外层顶层控制器现已确认会单独消费当前 RideOff 队列项的
`entry+0x88 = ActionGraphResult+0x50`：该值非负时会重置下级临时结果的同步
generation/frame，并在 generation 不匹配时维护下级队列。它与已经到达末端的
`evaluationEndSeconds(+0x4C)` 不是同一通道；同步传播 helper 已把它精确闭合为
仅在非负时生效的同步区间重基准覆盖值。DS2 宿主进一步确认它只能由
`PushActiveState` 保存的 `TransitionProperties+0x08` 经队列项 `+0x98` 一次性输出，
descriptor evaluator 和 `reachedEnd` 都不会生成它。因此不能把自行写入
`result+0x50` 当作动作完成修复。

精确 RideOff 顶层控制分支本身也已排除为末端消费者：它只读取当前项 timeState 的
frame generation／瞬时标志、`result+0x50` 和 `+0x54` gate，不读取
`reachedEnd` 或 `result+0x48/+0x4C` 来退役顶层状态。动作所有权释放因而位于
RideOff 叶子图和这个顶层控制分支之外。

新的只读样本 `dismount_20260726_184026_386` 已闭合自然完成边界：顶层队列从
`clock/derived/mapped=2.08959/2.08959/1.9674` 进入
`2.11045/2.1021/2` 时，`count=1`、RideOff key `0x0184F189`、
`EventSpace=0` 和转移标志均未变化，清理槽调用前后也完全相同；直到
`clock=2.38989` 仍保持饱和值。自然到达 `2.1021s` 因而只会钳位派生时间和映射值，
不会退栈或释放 RideOff 动作。完整运行证据见
[FastVehicleRideOffStateQueueRuntime.md](FastVehicleRideOffStateQueueRuntime.md)。

## 下车专题文档

- [FastVehicleRideOffRejectedTimeAcceleration.md](FastVehicleRideOffRejectedTimeAcceleration.md)：
  已证伪并撤回的 delta、Graph、descriptor 与姿态管线时间快进实验。
- [FastVehicleRideOffLifecycleAndActionEntry.md](FastVehicleRideOffLifecycleAndActionEntry.md)：
  RideOff 状态提交、OnEnter、RunPresentation、finalizer、detach、动作入口和流程候选。
- [FastVehicleRideOffActionGraphTiming.md](FastVehicleRideOffActionGraphTiming.md)：
  fullgame 长 descriptor、DS2 宿主 evaluator、结果时间区间与 motion payload。
- [FastVehicleRideOffStateQueueRuntime.md](FastVehicleRideOffStateQueueRuntime.md)：
  顶层/下级状态队列布局、转移槽与清理槽的运行时证据。
- [FastVehicleRideOffRootMotionAndLanding.md](FastVehicleRideOffRootMotionAndLanding.md)：
  侧向分类、SkinnedModel/mover/PhysicsCharacterMover 位移链和错误落点证据。

## 测试与证据口径

根目录 `test_boarding.ps1` 自动执行 ESC、移动、交互、下车和退出。早期首次测试中 ESC
之后由用户手动操作的部分不能作为自动化证据。

`FLOW_PASS` 只表示流程捕获完成。当前 `test_dismount.ps1` 还必须确认队列时钟同步、
原生 RideOff OnExit、600ms 内出现 Basic OnEnter，并在随后 S 控制窗口内确认没有
Fall OnEnter。任何一项缺失都判失败；`reachedEnd=1` 不能代替这些验证。

下车与控制截图都只保存按键前帧及 `25/50/100/200/400ms` 短里程碑，并以
`capture_manifest.csv` 的实际捕获中点计时。当前样本另以动画 observer 中的 mover
坐标确认 Basic 接管时已经到稳定地面高度，再用 S 序列的 100/400ms 画面验证实际位移。

## 失效保护边界

实现不修改寄存器或可执行代码字节，也不以固定 RVA 构造运行时 hook 目标。DS2 类函数
入口由唯一模式或精确 RTTI/COL 定位；fullgame 调用点由唯一模式定位并交叉校验到同一
可写 evaluator 槽。任一必需组件定位失败时，required-component mask 不成立，wrapper
只调用原函数。

快速上车会话窗口为 5 秒，所有对象、manager、plugin、玩家、车辆、CutIn 实例与 action
hash 都必须匹配同一有界会话。会话外共享 vtable 调用始终回到原函数。

当前卡车驾驶位快速下车已通过原生退出、Basic 接管、Fall 缺席与落地后 S 移动验证。
历史 Graph 单帧末端的 `end=0`/`end=1` 两版仍因动作冻结而判定失败；当前实现没有
恢复它们，而是在终点前停止额外推进并保留原生最后阶段。其他载具或座位变体未经运行
验证时仍不得标记为已覆盖。
