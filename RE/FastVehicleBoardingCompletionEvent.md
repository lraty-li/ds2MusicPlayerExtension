# 快速上车：动画完成事件与原生状态转移

## 结论

玩家上车并不通过乘客货物的 `MountableComponent_StartMount` 路径。玩家的核心路径是：

1. `DSPlayerVehicleRideOnState` 建立载具附着和座椅动作。
2. 动画图实时求值并产生 `SkeletonAnimationEventTag`。
3. RideOn Update 查询外部参数 `0xED`。
4. 查询为真时进入原生完成块，并请求 `RideOn -> Drive`。

左前方上车的历史只读基线中，原生 `0xED` 在约 `3.583s` 才变为真。早期实验门控
只改变该查询的返回语义，使原生完成块在约 `0.025s` 被执行。三方向现场对照已经证明，
这个实验只提前逻辑 Drive，不会缩短可见动画，因此它不是当前快速上车实现。

2026-07-18 当前实现先让活动玩家 descriptor 由原 evaluator 以缩放时间正常求值到末端，
再在同一次 RideOn Update 中让原生事件进入公共完成块。正前方最终测试在 RideOn
elapsed 约 `0.104s` 进入 Drive。车辆机械座椅是另一条独立状态机：正前方 truck
pending `3/4` 已在消费前从完成的基线状态安全取消，不由 `0xED` 完成。

## 调查方法

- 运行时验证统一使用 `test_boarding.ps1`，自动执行启动、当前测试位置上车、下车和退出；
  本轮运行时 approach `2` 直接确认当前位置为正前方，控制台的 `left-front` 文案只是
  脚本沿用的静态标签。
- 静态分析只使用已知地址的反编译、限定地址范围的指令查询和定向交叉引用。
- hook 入口全部使用唯一模式匹配，不使用版本相关的固定 RVA。
- IDA 数据库同步补充函数名、参数类型和关键控制流注释。

## 动画事件产生链

### ActionParams 映射

`ActionParams_QueryBoolEventByParamId` 位于当前数据库的 `0x140DBEA20`。它先通过虚表 `+0xA0` 映射外部参数，再调用 `GraphAnimationManager` 虚表 `+0xE0` 查询事件。

运行时确认：

- 外部参数：`0xED`
- `ActionParams_MapExternalParamId`：`0x140DBB9A0`
- 内部事件 ID：`186`
- 事件管理器类型：`GraphAnimationManager`

映射函数直接读取 `actionParams + 0x378 + 4 * externalParamId` 的表项。

### 实时动画图求值

`GraphAnimationManager_EvaluateFrame` 调用 `AnimationGraphEvaluationContext_Begin` 建立求值上下文，再由 `AnimationGraphEvaluation_Run` 写入时间和图索引参数。随后 `AnimationGraphInstance_Evaluate` 从图资源描述符的 `+0xD8` 项派发实际图求值。

姿态抽取函数 `GraphAnimationManager_EvaluateNodePose` 只负责临时内存、图节点求值和可选姿态输出，不是事件列表的所有者。这解释了为什么清除姿态、抑制座椅片段或 presentation 没有改变上车完成时间。

### SkeletonAnimationEventTag 发布

图求值完成后，`GraphAnimationManager_CollectSkeletonEventTags` 遍历各上下文输出：

- 对每个输出记录调用虚拟类型标识函数。
- 只保留类型标识等于 `g_SkeletonAnimationEventTagTypeId` 的记录。
- 每个保留记录为 16 字节。
- 记录被发布到 `GraphAnimationManager` 的主上下文或子上下文事件列表。

RTTI 注册函数明确给出类型名 `SkeletonAnimationEventTag` 和 `UUIDRef_SkeletonAnimationEventTag`，因此 `0xED` 不是普通持久布尔参数，而是由动画资源在特定帧发出的动画事件标签。

## RideOn 完成控制流

`DSPlayerVehicleRideOnState_Update` 在 `0x140F99FEC` 查询参数 `237`，即 `0xED`。

精确指令控制流为：

- 查询结果为真：`0x140F99FF3` 跳到原生公共完成块 `0x140F9A0D7`。
- 查询结果为假：继续检查 runtime 标志、计时器和其他安全条件。
- 公共完成块最终在 `0x140F9A2E7` 写入 `plugin + 0x11A = 2`，请求 `RideOn -> Drive`。

真分支到公共完成块之间没有 `runtime + 0x18B` 的前置检查。因此，返回真可以复用完整原生状态转移，而不是像直接写 `next=2` 那样绕过 Update 的通知、任务和清理逻辑。

## 历史事件门控实验

历史事件门控实验曾在参数 ID 为 `0xED`、活动 RideOn 快照满足
`current=1,next=1,stage=2` 时，把原生假结果提升为真。该实验只用于确认原生完成块的
控制流语义，不再是当前实现。

三方向现场对照已经证明单独门控不会缩短可见动画。2026-07-10 的
`BoardingCompletionGate` 只读基线调用原函数并原样返回 `nativeResult`，得到约
`3.587s` 的原生完成时间。

2026-07-18 当前功能构建改为包装 `GraphAnimationManager` primary vtable slot 28。
它只在 RideOn Update TLS 范围、同一玩家 manager、内部事件 ID `186`、context 0、
合法 stage 2 且 `b18B=1` 时协调结果。角色 descriptor 已由原 evaluator 正常求值到
末端、原生事件已经出现后，wrapper 在同一次 RideOn Update 内放行事件；原函数进入
公共完成块并请求 `next=2`。正前方自动测试在 RideOn elapsed 约 `0.100s` 进入 Drive。

当前顺序不再让 Graph 事件等待 CutIn Deactivate。运行时和静态分析已证明，正前方
CutIn 的 Deactivate 会读取玩家当时的 world basis 计算驾驶镜头 handoff；若先结束
CutIn，读取到的是上车动作留下的倾斜 basis。当前实现先让 RideOn 正常进入 Drive，
等待原生 `DSPlayerMoverAccessor` ModifyAnimatedPose 提交驾驶姿态，再推进 CutIn 到
finished 并由 CameraMode 调用原生 Deactivate。Graph 事件仍只负责 RideOn 状态完成，
没有被当作 CutIn 完成条件。

### 三方向现场验证

以下内容是历史“只强制完成事件”实验的三方向验证。同一运行中的四次上车记录覆盖了
`approach=0`、`approach=1` 和 `approach=2`。三类记录具有相同的关键行为：

```text
SeatTransition callback vtable = DSPlayerVehicleDriving
forced native boarding completion elapsed = 0.025..0.029s
DriveEnter exit elapsed = 0.025..0.029s
```

用户逐方向对照结果：

- 从左侧上车：动画无变化。
- 从右侧上车：动画无变化。
- 从正前方上车：镜头略有变化，动画时长无变化。

这将 `0xED` 的作用限定为三方向共享的 RideOn 逻辑完成通知。`ClassifyBoardingApproach` 的分流会改变 seat state，但强制同一个完成通知不会推进三个动作各自的可见时间轴。

当前生产实现不再只强制该通知：玩家 descriptor 已先由原 evaluator 到达合法末端，
事件 wrapper 只协调已经出现的原生事件；CutIn 由自身 playback 到达 finished；
`DSVehicleTruck` 机械状态则在自己的 request 消费边界处理。三层完成条件互不替代。

## 窗口消失与崩溃判定纠正

早期自动化把启动时缓存的 HWND 当作永久句柄。句柄短暂失效时，即使 DS2 的 PID 仍存活，脚本也会进入失败分支并调用 `kill_ds2.ps1`。因此当时观察到的“窗口消失”和进程退出不能作为游戏崩溃证据。

VEH 同期记录到的访问异常是 first-chance 通知；处理器返回 `CONTINUE_SEARCH`，日志本身不能证明异常最终未被游戏处理。把它与脚本主动杀进程合并解释为插件崩溃是不正确的。

`test_boarding.ps1` 已改为先检查 PID，并在旧 HWND 失效后最多等待三秒重新绑定 `MainWindowHandle`。修正后相同插件构建完整通过上车、Drive、下车与退出流程。这证明此前失败至少包含确定的脚本误杀因素，也排除了“窗口句柄变化等于游戏崩溃”的判定方式。

## 上车与下车输入隔离

旧事件门控脚本一看到 `DriveEnter exit` 就发送下车 `F`。门控把 Drive 提前到约 `25ms` 后，这个输入会与仍在收敛的上车画面重叠，表现为“短暂扒车、瞬间到座位、马上播放下车”。这不是状态机自行越过下车状态。

脚本现已在 Drive 确认后增加四秒 `Boarded dwell`。隔离后的自动化测试确认：

- 四秒停留期间游戏保持车内状态。
- 随后发送下车 `F` 才出现 `start=0 finishFlag=0`。
- 上车、停留、下车和退出完整通过。

因此此前视觉上的“立即下车”来自测试输入时序，而不是完成事件门控自动触发了 RideOff。

## 提前完成座椅控制器的崩溃分析

运行时 vtable 与 RTTI 确认，玩家左前上车使用：

- callback 类型：`DSPlayerVehicleDriving`
- callback update：`0x141F4E8B0`
- callback reset：`0x141F50340`
- controller start：`0x141F94070`
- controller finish：`0x141F6BA80`

一次受限实验在原生 `ProcessAttach` 已达到 `stage=2` 后，调用同一个高层 `SeatController_StartOrFinishTransition(start=false, finishFlag=true)`。调用返回成功，但约 `0.65s` 后在 `0x141F84D80` 崩溃。

IDA 与栈回溯确认调用链为：

```text
SeatController_UpdateFrame
  -> SeatController_UpdateTransitionAudio
  -> 读取 seatController+0x5C0 active callback
```

提前 finalizer 已清除 `seatController+0x5C0`，但另一工作线程上排队的控制器帧仍会无空值检查地读取 callback。因此外层 RideOn `stage=2` 不是座椅控制器的内部终态，`finishFlag=true` 是终态清理接口而不是完成请求接口。该主动 finalizer 已从运行构建移除；IDA 已在清指针点、排队更新调用点和故障读取点记录此约束。

## `DSPlayerVehicleDriving` 回调排除

IDA 构造函数 `DSPlayerRideVehicleActionPlugin_Ctor` 确认：插件在 `+0x2A8` 内嵌
`DSPlayerVehicleDriving`，`ProcessVehicleAttach` 启动座椅过渡时把这个子对象作为
callback 传入。虚表槽 1 为 `DSPlayerVehicleDriving_UpdateTransition`。

只读 hook 使用该函数入口的唯一模式匹配，并严格过滤到活动 RideOn 对应的
`plugin+0x2A8`。左前自动化结果：

```text
runtime+0x18B 0->1                         elapsed=3.24492
DrivingProgress first call p14=1 flags=4  elapsed=3.25743
DrivingProgress p14=1 flags=4             elapsed=3.48683
native 0xED / DriveEnter                   elapsed=3.58693
```

因此这个回调在约 `3.245s` 的 mount-side 完成之后才开始更新；`callback+0x14` 从首次
命中就是 `1`，直到 Drive 前保持不变。它参与座椅过渡完成/音频状态，但不是此前约
三秒可见攀爬动画的时间轴，也不是动画所有者。

修正调用者采集后，所有命中都来自 `0x141F62516`，即
`SeatController_UpdateActiveTransitionCallback` 的虚表槽 1 调用返回点。该调用只在
`seatController+0x4FC != 0`、`+0x5C0 callback != null`、`+0x4E4 mode == 2` 且
`callback+0x28 == 0` 时执行；随后把 callback `+0x08..+0x27` 的输出复制到 seat action。
这条静态门控与运行时首次命中时间一致。

## 历史事件门控运行证据

2026-07-10 历史事件门控实验的通过日志：

```text
stage 1->2 elapsed=0.0208542
boarding completion query native=0 eligible=1
forced native boarding completion event elapsed=0.0291959
RideOnState transition next 1->2 elapsed=0.0291959
DriveEnter entry elapsed=0.0291959 b18B=0
DriveEnter exit elapsed=0.0291959 b18B=1
```

`test_boarding.ps1` 最终确认：

```text
Confirmed RideOn stage 0->1
Confirmed DriveEnter
Confirmed seat transition finish
PASS: board, drive, dismount, and quit confirmed
```

相对只读基线的约 `3.583s`，Drive 转移提前到约 `0.025s`。DriveEnter 原生逻辑在同一帧把 `b18B` 从 `0` 更新为 `1`，说明门控没有要求插件伪造该完成字段。

画面采样结果为：

| 采样时间 | 可见状态 |
| --- | --- |
| 上车前 | 角色站在车辆左前方 |
| `150ms` | 已进入原有攀爬动作 |
| `500ms` | 仍在车辆侧面/座位上方攀爬 |
| `1200ms` | 仍在向驾驶舱内移动 |
| `2200ms` | 已直接收敛到座位，仍处于侧面镜头 |
| `3700ms` | 已进入后方驾驶镜头 |

结合三方向原版对照，可验证的结论仅是历史实验把逻辑 Drive 提前到约 `25ms`；不能把
`2200ms` 的单组截图解释成动画缩短。`0xED`、座椅 finalizer 和
`DSPlayerVehicleDriving` callback 都不是长动画时间轴的单独所有者。

旧记录中的 `presentation request` 已被重新闭合为 `DSCutInCamera` 请求队列。它会
选择独立的 CutIn action variant，并以自身 elapsed/duration 判定完成；它不消费
ActionGraph `0xED`。因此强制 `0xED` 只能推进 RideOn 正常完成块，不能据此结束
已经激活的 CutInCamera。完整相机退出由 CutIn finished、CameraMode 模块切换和
`DSCutInCamera_Deactivate` 共同完成。

## 当前正前方运行证据

最终功能构建的关键顺序为：

```text
Truck pending 3 cancelled at current 0 / controller playback state 2
player descriptor complete at duration 0.0550029
native internal event 186 released
RideOn completion Update returned
Drive Enter at RideOn elapsed 0.0959293（不同已验证运行约为 0.096..0.104s）
CutIn playback finished
CutIn Deactivate clean=1
native dismount consumes Truck state 7
DLL_PROCESS_DETACH
```

同一轮 `test_boarding.ps1` 完整 PASS，没有 CrashTrace。同步录屏没有出现驾驶座下降的
中间帧。卡车机械状态的对象布局、RTTI、请求消费和 OnExit 回位链记录在
[FastVehicleBoardingTruckMechanicalAnimation.md](FastVehicleBoardingTruckMechanicalAnimation.md)。

全组件失效保护加入后的首次运行没有出现 wrapper 主动放行事件的日志，但 RideOn Update
已经通过另一条原生条件写出 `next=2`，随后 Drive、pose、CutIn finished 和 Deactivate
全部完成。脚本因只接受固定事件日志而报断言失败。相同二进制重跑出现事件日志并完整
PASS；两轮均无 CrashTrace。这限定的是测试日志断言的调度敏感性，不改变 `0xED` 及
原生公共完成块的语义结论。
