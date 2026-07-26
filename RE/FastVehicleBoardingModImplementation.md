# DS2 玩家快速上下车：当前状态与知识索引

日期：2026-07-26

> **当前结论：**快速上车已经验证；快速下车仍未完成。Graph 末端区间候选虽然能在
> 117ms 内让玩家离座并沿原生根运动落到车旁，但用户实际操控确认下车后玩家动作完全
> 卡住，无法执行任何动作，因此它只是新的视觉假阳性，已经判定失败并必须撤回。
> 旧 `PASS`/`FLOW_PASS` 和关键帧都不能单独证明 Mod 可用；快速下车现在还必须验证
> 落地后的玩家操控恢复。所有已证伪时间倍率、重复求值、直接绕过以及本次 Graph
> 末端区间候选均不得作为完成方案。该失败不否定 ActionGraph 末端推进方向；定点
> 静态分析已经确认旧实现把终点写成“恰好等于 duration”，因而得到末端姿态却没有
> 触发原生 `reachedEnd`。后续严格越界候选已经让原生 evaluator 返回 `end=1`，但
> 自动 W 输入探针仍确认玩家冻结，因此 `reachedEnd` 不是缺失的唯一完成条件。
> 随后把 RideOff 四级结果合并从 `0x73` 扩展到 `0x7F` 的候选也已由运行日志证伪：
> 第一层来源区间为 `0..2.1021`，目标在合并后仍为 `0..0.00834168`，所以它没有把
> 叶末端同步区间送到外层，也不能解除动作锁。

## 当前部署状态

本地构建与游戏目录中的当前 ASI 已重新核对一致：

```text
size    = 339968
SHA-256 = FB478C161838793F87751175EA4AF85D18C033FAF50EFA83B39A3F690EEA0BF1
```

游戏目录现在部署的是无冻结旧安全路径加只读 fullgame 状态队列观测器：保留已验证
快速上车，并在第一次原生 RideOff RunPresentation 后调用原生 finalizer，让状态、
CutIn 和镜头快速退出，但不改写长 ActionGraph 结果的时间区间。观测器只透传
`qword_1884D2100` 和 `qword_1884D2118`，不修改状态队列。该构建仍会播放约 2 秒
玩家下车动作；另一个只读 wrapper 观察 `GraphAnimationManager` slot 28 的原生
布尔事件，同样不改写返回值。该构建因而不是最终快速下车方案。

已经证伪的完整区间候选有两个连续构建：
`57D8BEE6257F974CB211F86CB267515FADA661AB3A8E0C39A7F2F59F949892C8`
（333824 字节）完成自动流程与视觉关键帧，随后
`0C135ACBE3973BD58F3AB2D2392D92C64DA5FDD0136D8A3FCCAC70061A745D05`
（333312 字节）只删除误导诊断读取。用户实际操控确认后者会完全锁死玩家动作。当前
源码与工程已经删除 `RideOffGraphEndpoint`，也没有恢复会让角色站到车上的
`RideOffStateBypass`。规定构建输出 `BUILD_OK`，安全基线部署源与目标哈希一致。

随后用于验证严格越界条件的构建为 334336 字节，SHA-256
`9418291192221BA395F3EAB1518C0C6C5C13598B92B61C5DDD69AE06FACB1432`。
它由原 evaluator 正常产生 `end=1`，但落地后的 W 探针仍无响应，因此同样已经证伪。
游戏目录已再次恢复上方 339968 字节安全诊断构建；该失败构建不得继续部署。

同步通道合并候选为 348160 字节，SHA-256
`42644E2D90E36B048D7CEF22D93EB39ED21FA15D5590E633E4BA60A78F1F0273`。
样本 `dismount_20260726_162700_154` 确认它命中实际四级 RideOff 路径，但首级
`0x7F` 合并没有改变目标的单帧同步区间，后续层也只能继续传播旧区间。该候选已经
判定失败；相关源码和工程项已经删除，并已重新 `BUILD_OK`。游戏目录的源/目标哈希
均已核对为上方当前安全诊断构建，失败 ASI 不再部署。详细运行证据见
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

## 快速下车：视觉跳过成立，但功能失败

有效视觉基线为 `artifacts/boarding/dismount_20260725_215713_944`。以
`capture_manifest.csv` 的实际捕获中点为准：

```text
约 70ms       腿部仍在车辆内
约 1.508s     仍处于下车动作
约 2.016s     才完整站到车外
```

现有安全路径在第一次原生 RideOff RunPresentation 后调用原生终结器，并只在
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
320ms、726ms 均未再出现车辆下车姿态，角色没有冻结，也没有站到车体上。只查看了
这三张首选关键帧，没有为得出结论追加查看 1200/2000ms。

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
当前失败属于“生成了末端结果但没有声明原生结束”，不构成对末端推进方向本身的否定。

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
在已确认的 `0x0184F189` RideOff 条目分派之后，于 `0x183607896` 调用宿主解析槽
（哈希 `0x49589132`）进行这张动态表的 post-evaluate 处理。此前标注的
`0x18360CCE8` 属于后续另一张生成式动态表，现已从 RideOff 路径中排除。因此现有
`end=1` 日志只闭合叶 descriptor，不闭合动态表输出或玩家顶层动作退栈；这比
“完成位没有写入”更准确地限定了当前缺口。

这张表的实例来源也已闭合：`PlayerActionGraph_EvaluateFrame_Stage1` 在
`0x183162E41` 从 Graph 运行时状态基址取 `base+0x5730`，随后依次作为 Stage2
`arg_69B0`、`sub_183594DF9` 的 `arg_5B8`、以及 `sub_183605C47` 的第 17 个
参数传入。Stage2 在 `0x18318C135` 对同一对象调用解析槽
`qword_1884D2108`，初始化／提交默认 key `0x78EC48BF`；相关槽由初始化器 hash
`0x6685E865` 解析。该 key 的具体玩法名称尚未闭合，不能提前命名为 Free。由此确认
`0x183607896` 操作的是玩家 Graph 持久状态块中的特定状态队列，而不是 wrapper
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

同一运行时专题还证伪了照搬快速上车完成事件的方向。样本
`artifacts/boarding/dismount_20260726_160834_062` 中，
`GraphAnimationManager` slot 28、`contextIndex=0` 的原生 true 事件只在 RideOff
开始后 0–15ms 出现；约 2 秒动作自然释放时没有新事件首次变为 true。因此不能通过
提前放行该接口中的某个布尔事件来补齐快速下车动作解锁。

Stage2 收尾汇编还确认，顶层 `qword_1884D2118` 的非零返回值没有被消费：
`0x1831D7FDC` 调用后，`0x1831D7FE2` 立即装入下一张队列并继续调用同一槽，既不读取
也不保存 `RAX`。因此该返回指针不是遗漏的 active action 句柄。

该宿主槽还接收独立的 `deltaSeconds`。失败候选把叶区间一次推进到 `2.1021s` 时，
`0x183607896` 仍向 RideOff 所在动态表的 post-evaluate 传入原始单帧增量。因此即时末端姿态与外层
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
定位当前 RideOff action 的完成事件或所有权释放链。

fullgame 精确控制流已经排除 RideOff 叶子图内部存在这个释放消费者：从叶 evaluator
返回到第一层 `0x73` 选择合并的实际路径不读取叶 timeState、`reachedEnd` 或
`evaluationEndSeconds` 作退役判断；继续到该子图最终合并点也只有活动门、
动态表 post-evaluate 和结果选择。叶结果的末端标志会向外传播，但释放当前 action
的逻辑必定位于 `PlayerActionGraph_Subgraph_0184F189_Evaluate` 之外。

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

同时必须限定这份自动化日志的证明范围：`capture_manifest.csv` 的最后一张截图中点是
`2.024s`，状态队列最后一个里程碑是 `2.01034s`，二者都早于 descriptor 的
`2.1021s` 自然末端，脚本随后很快退出。因此该样本不能单独证明自然末端之后仍冻结；
“下车后完全无法动作”目前来自用户随后进行的较长时间手动验证。后续只读观测若要闭合
自动证据，必须覆盖 `2.1021s` 之后，但不增加既定截图里程碑。

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

`FLOW_PASS` 只表示自动流程捕获完成，不表示下车骨骼动画已经跳过。视觉成功必须同时
满足：100ms 已在车辆外，且 300ms、700ms 没有下车姿态、冻结或错误落点。功能成功
还必须证明落地后的移动/普通动作输入已经恢复；`reachedEnd=1` 也不能代替该验证。

截图只保存按键前帧及 `100/300/700/1200/2000ms` 里程碑，并以
`capture_manifest.csv` 的实际捕获起止和中点计时，不依赖文件名。先检查
100/300/700ms；只有结论矛盾才查看 1200/2000ms。

针对已出现的操控冻结矛盾，测试辅助会在下车后 800–1700ms 自动按住 W，并使用既有
1200/2000ms 里程碑与 700ms 前帧比较；不为控制验证增加新的截图时点。

## 失效保护边界

实现不修改寄存器或可执行代码字节，也不以固定 RVA 构造运行时 hook 目标。DS2 类函数
入口由唯一模式或精确 RTTI/COL 定位；fullgame 调用点由唯一模式定位并交叉校验到同一
可写 evaluator 槽。任一必需组件定位失败时，required-component mask 不成立，wrapper
只调用原函数。

快速上车会话窗口为 5 秒，所有对象、manager、plugin、玩家、车辆、CutIn 实例与 action
hash 都必须匹配同一有界会话。会话外共享 vtable 调用始终回到原函数。

快速下车目前只通过自动流程、原生退出日志和最小视觉关键帧，未通过落地后的玩家操控
验证。Graph 末端区间的 `end=0` 与原生 `end=1` 两版均已因动作完全卡住而判定失败，
不能作为可用 Mod。
