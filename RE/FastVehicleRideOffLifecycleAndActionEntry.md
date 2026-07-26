# 快速下车：RideOff 生命周期与动作入口

日期：2026-07-26

返回[当前状态与知识索引](FastVehicleBoardingModImplementation.md)。

> **结论：**RideOff、CutIn、finalizer 和 Free 状态可以即时闭合，但状态完成不等于
> 玩家骨骼动作或安全落点完成。已知的 `state=4`、`request=1` 与车辆 request `7`
> 都不是玩家下车动作的充分且唯一入口。

## 已确认原生入口

- RideOff primary vtable slot 11：OnEnter。
- slot 13：Update。
- slot 14：RunPresentation。
- `RideRuntime_FinalizeRideOffPresentation`：`0x1410103D0`，唯一签名
  `48 89 6C 24 ? 57 48 83 EC 20 48 8B 41 ? 0F B6 EA`。
- 终结器参数 `a2` 表示 `runtimeMode == 3` 特殊路径；`a3` 才是 force，可绕过
  `runtime+0x7380 bit30` 外部门。
- `RideRuntime_UpdateDismountDetach`：`0x1410105A0`，唯一签名
  `40 53 48 81 EC A0 00 00 00 48 8B D9 84 D2`。
- `runtime+0x371 != 0` 会进入原生 operation 21 安全离车路径；
  `forceDetach=1` 仅在 `runtime+0xB8` 所指对象的 `+0x160 >= 0x14` 时形成另一入口。

当前安全基线先完整执行第一次原生 RunPresentation，再调用终结器，并让动画 ready
查询只在 Presentation TLS 中通过。这能快速进入 Free 并正常结束 CutIn，但不会取消
已经建立的玩家下车动作。

## RideOff 状态完成与视觉完成的区别

下车不是 RideOn 完成事件的反向复用。`DSPlayerVehicleRideOffState` 的 primary vtable
三个相关槽已静态确认：slot 11 为 Enter，slot 13 为每帧 Update，slot 14 为
RunPresentation。slot 13 只累计下车状态时间并驱动 detach/progress；slot 14 创建下车
CutIn、维护展示状态，然后调用 runtime 终结器。

终结器 `sub_1410103D0` 的有效条件是：

```text
(runtime+0x7380 的 bit 30 已置位) || a3 != 0
```

随后它查询 `a1[11]` 动画组件的 vtable `+0x3E0`。查询通过后，终结器由原生代码请求
动画状态 1、执行组件复位，并完成运行时展示清理。参数 `a2` 不是强制开关；它对应
RunPresentation 中的 `runtimeMode == 3` 特殊路径。参数 `a3` 才是 force。

当前实现先完整执行一次原 RunPresentation，使原生下车 CutIn/action 已建立；随后仅在
当前 RideOff 会话内让该动画组件的就绪查询返回 true，并以
`(runtime, runtimeMode == 3, 1)` 调用经唯一签名解析的终结器。组件状态 1 和 CutIn
的完成/Deactivate 仍由原函数处理；卡车机械下车 request `7` 由独立车辆 wrapper 在
消费前抑制。会话外的同一共享 vtable 始终调用原查询。

该路径确实把 RideOff 状态 4 和终结器状态 1 压缩到约 `0/16ms`，并正常完成 CutIn 和
Free 镜头；但按键同源时钟截图显示玩家下车骨骼动作仍持续约 2 秒：

```text
dismount_20260725_215713_944
约 70ms       腿部仍在车辆内
约 1.508s     角色仍处于下车动作中
约 2.016s     角色才完整站到车外
```

所以 `elapsedMs <= 750` 只能约束状态机，旧 PASS 属于视觉假阳性，不得再作为“下车
动画已跳过”的结论。首次 ESC 后由用户手动继续的运行也不属于全自动证据；后续脚本
已自动执行 ESC、移动、交互和退出。


## 状态、请求与动作入口
进一步沿 `DSPlayerMoverAccessor` 的 primary vtable 定位到 Mover 的独立生命周期入口：
accessor slot 3 转发到 `DSPlayerMover` slot 45（`0x140EC80C0`）。该函数把收到的
RideOff 动画状态保存到 `mover+0x2E0`，再调用 mover slot 61 的状态回调；当前派生类
中的该回调为空实现。slot 49/50 才分别是 PreModifyAnimatedPose 和
ModifyAnimatedPose。RideOff OnEnter 另通过 accessor slot `0x2A8` 把 ride request `1`
写到 `mover+0x59C`，把 option 写到 `mover+0x5BD`；清理路径中的 slot `0x2F0`
只清除这两个待处理字段。运行既证明事后清除不能取消视觉动作，也证明在 OnEnter 内
阻止 `request=1` 写入仍不能阻止视觉动作。静态复核进一步定位到一个实际消费者：
`DSPlayerMover_OnModifyAnimatedPose` 在 animation state `3` 分支的
`0x140EC857A` 读取 `mover+0x59C`；值为 `1/2` 时只清零运动参数
`mover+0x53C/+0x4C8`，随后进入普通运动处理 `0x140ECECA0`。因此这个字段不能再被
视为建立或取消 Graph 下车动作的充分入口。

完整复核 `DSPlayerMover_OnModifyAnimatedPose` 的 438 条指令后，确认该函数没有读取
相邻的 `mover+0x5BD` option。accessor 虚表 `+0x2B0` 的
`0x140DBA990` 也已命名为 `DSPlayerMoverAccessor_GetRideActionRequest`；它只返回
`mover+0x59C`，不返回 option。因此在当前已经闭合的 mover 消费链中，`option=0`
不是可改成其他值以触发“立即完成”的开关；这一结论不扩大为尚未逐一闭合的其他系统
消费者。

更正：RideOff OnExit 本身调用的是 accessor slot `0x3C8`
（`0x140DBB1D0`），现已命名为
`DSPlayerMoverAccessor_SetRideOffBlendWeight`。RunPresentation 先把
`2 * runtime+0x1B0` 限制到 `[0,1]` 后逐帧写入 `mover+0x804`，OnExit 再用同一
setter 写成 `0`。因此该字段是 RideOff 演出混合权重，不是动作取消入口，也不承载
侧向落点；OnExit 没有调用 slot `0x2F0`。请求 `+0x59C/+0x5BD` 的清理由终结/其他
清理路径承担。Mover 的
ModifyAnimatedPose 会通过 slot 47 读取当前动画状态并按 `1..5` 选择 root-motion
应用分支；状态 1 仍是有效的运动分支，所以提前请求状态 1 本身不会取消已经实例化的
下车图动作。原生强制离车路径会以 `forceDetach=1` 调用
`RideRuntime_UpdateDismountDetach`，后续调查以该路径是否还包含动作中断为准。

该强制路径已经定位为 `DSPlayerVehicleDriveState_Update`
（`0x140F8FB60`，Drive vtable slot 13）内部的异常离车分支。它在仍处于 Drive、
尚未执行 RideOff OnEnter、也尚未写入 request `1` 时调用
`RideRuntime_UpdateDismountDetach(runtime, 1)`，随后处理任务通知和状态推进；分支中
没有“取消一个已经活动的 RideOff 图动作”的调用。因此它只证明异常/失效车辆上下文
存在无 RideOff 的直接 detach 生命周期，不能直接推导正常下车的安全落点也已完成：
后续正常下车运行把同一 detach 前移到 RideOff 提交前，角色虽瞬间离座，却站到了车体
上。另一次运行还证明，在 RideOff OnEnter 内门控当前已知的 request/state 并复用
detach，视觉动作仍会出现；所以 `state=4` 与 `request=1` 不能被视为建立玩家下车
动作的充分且唯一入口。

该 Drive 异常分支在 detach 后调用的 `0x140F95740` 也已完成指令级复核并命名为
`DSPlayerVehicleDriveState_TrySendDirectDetachMissionNotify`。它只在特定
车辆/SeatAction 条件满足时构造任务数据并发送 `MsgMissionPlayerNotify`；函数中没有
玩家世界变换写入、落点计算或车辆状态提交。因此失败候选虽然跳过了这个辅助调用，
但它不是缺失的落地后处理，也不能解释或修复“瞬间站到车顶”。原生异常 detach 分支
仍未提供可复用的正常下车车外端点。

RideOff `RunPresentation` 还存在一条独立于上述 OnEnter 写入的动作路径：
`0x140F980B7` 调用 `DSCutInCameraRequestBroker_QueueAction`，随后把 hash 保存到
`rideOff+0x1AC` 并将 `rideOff+0x1A0` 置为已初始化。本次车辆类型和 variant `2`
选择的 hash 是 `0x15371611`，与运行日志中的 RideOff CutIn action 完全一致。这确认
前置抑制候选之后仍有一个原生 action 入队点；现有证据只证明它创建了实际观察到的
CutIn action，尚不能把玩家骨骼动作单独归因于该调用。

### 2026-07-26 RideOff 状态提交边界

`DSPlayerRideVehicleActionPlugin` 的车辆子状态调度链已经静态闭合。构造阶段把每实例
dispatcher 初始化为空实现；`DSPlayerRideVehicleActionPlugin_CreateVehicleStates`
（`0x14100C0D0`）随后建立永久子状态对象：

```text
plugin+0x150  RideOn
plugin+0x158  Drive
plugin+0x160  RideOff
```

`DSPlayerActionPlugin_CommitPendingState`（`0x14111F940`）从 `plugin+0x118` 读取当前
状态、从 `plugin+0x11A` 读取待提交状态，待二者不同时调用插件虚表 `+0xB8`。
车辆插件的该槽是 `DSPlayerRideVehicleActionPlugin_SwitchState`
（`0x140FE4580`），其顺序已经由指令级分析确认：

```text
旧 dispatcher，phase=1、delta=0
  -> DSPlayerVehicleState_DispatchPhase
  -> 旧状态 vtable slot 12（OnExit）

按 newState 选择并写入 plugin+0x140：
  0 = Free/no-op
  1 = RideOn dispatcher
  2 = Drive dispatcher
  3 = RideOff dispatcher
  4 = 内联车辆状态 dispatcher（0x14100AF50）
  5 = DSPlayerVehicleEscapeState dispatcher（0x14100B7A0，状态对象在 plugin+0x170）
  6 = 内联车辆状态 dispatcher（0x14100B7B0）
  7 = 内联车辆状态 dispatcher（0x14100BE50）

新 dispatcher，phase=0、delta=0
  -> 若 newState=3，则进入 plugin+0x160 的 RideOff 对象
  -> DSPlayerVehicleState_DispatchPhase
  -> RideOff vtable slot 11（OnEnter）
```

这里同时纠正了先前只列出状态 `0..3` 的不完整描述：选择表
`funcs_140FE45CA` 实际有八项。`DSPlayerRideVehicleActionPlugin_CreateVehicleStates`
还在 `plugin+0x170` 构造了 `DSPlayerVehicleEscapeState`；其 dispatcher
`0x14100B7A0` 明确转发到该对象，因此 Escape 对应状态 `5`，不是状态 `4`。
状态 `4/6/7` 均由各自内联 phase dispatcher 处理，不能在缺少进一步证据时套用
RideOff 或 Escape 的语义。

Escape 也已排除为“无演出安全落地”入口。其状态虚表 slot `11..14` 已分别命名为
`DSPlayerVehicleEscapeState_OnEnter`（`0x140F963F0`）、
`DSPlayerVehicleEscapeState_OnExit`（`0x140F96900`）、
`DSPlayerVehicleEscapeState_Update`（`0x140F96A90`）和
`DSPlayerVehicleEscapeState_RunPresentation`（`0x140F96C60`）。OnEnter 仍然：

```text
请求 mover animation state=4
  -> BeginSeatTransition(runtime, 0)
  -> 写入离车 action request=1
```

Update 随后才调用普通 `RideRuntime_UpdateDismountDetach(runtime, 0)`；
RunPresentation 仍推进离车混合并调用 `RideRuntime_FinalizeRideOffPresentation`。
其额外前置辅助 `0x141010A30` 只解析当前车辆、发送固定 `MsgDsNotify` 并置运行时标志，
未写玩家世界变换或车外落点。结合已经确认不计算侧向端点的
`BeginSeatTransition`，Escape 生命周期中没有独立的即时车外落点求解步骤；跳过其
演出仍会丢失本应由动作/root motion 产生的空间位移，不能用来修复“瞬间站到车顶”。

状态 `7` 也已排除为现成的安全落地状态。其 phase `0` 的虚调用最终只是
`0x14111F4A0`，把 64 位控制掩码写到插件 `+0x20`；phase `2` 仅调用普通
`RideRuntime_UpdateDismountDetach(runtime, 0)`；phase `6` 的
`0x141003D80` 只在车辆对象锁内写浮点值/标志并释放引用，然后标记状态完成。整条
dispatcher 没有玩家世界变换或车外端点写入。它可能服务于不同的车辆控制情形，但
不能提供当前缺失的即时地面落点，因而不得作为无依据的 RideOff 替代状态。

公共函数 `DSPlayerVehicleState_DispatchPhase`（`0x14101CDB0`）还确认 phase
`0/1/2/3` 分别映射状态虚表 slot `11/12/13/14`，即
OnEnter/OnExit/Update/RunPresentation。切换函数返回后，提交函数才把
`plugin+0x118` 更新为先前捕获的 pending state，并清除 `plugin+0x11B`。

因此，运行候选对 `state=4` 和 `request=1` 的门控确实发生在
`pendingState=3` 已选中且 RideOff OnEnter 正在执行之后；它没有覆盖 Drive 到
RideOff 的状态提交边界。这一调用顺序与“门控实际命中但视觉动作仍存在”的运行结果
一致，也进一步排除了把 `mover+0x59C` 当作唯一动作建立入口的解释。

另行复核纠正了旧命名：`0x140F98A60` 实际是
`DSPlayerVehicleRideOffState_RequestVehicleDismountAnimation`，不是玩家
`SelectDismountPoseVariant`。它把 side 映射为目标车辆自己的动画请求：

- `DSVehicleTruck` 分支使用 `truck+0x12F8` 动画控制器、
  `truck+0x1310` current 和 `truck+0x1314` requested；side `2` 请求状态 `7`；
- 另一车辆类型在目标对象 `+0x125C` 写入 side `0/1/2` 对应的状态 `13/14/12`。

当前有效样本 `dismount_20260725_215713_944` 的日志已明确记录
`FastRideOff TruckSeat dismount request suppressed`，条件为
`current=0 request=7 playbackState=2`，即 request `7` 在车辆消费者前确实被清掉；
但同一样本中玩家直到约 `2.016s` 才站到车外。因此该函数只控制车辆/机械座椅动画，
不是玩家骨骼下车动作入口，也不提供玩家的侧向落地点。旧注释中的 pose index 和
`runtime+0x2A4` 解释均已删除。


## 运行证伪：OnEnter 门控与提交前绕过

### 2026-07-26 OnEnter 前置门控运行证伪

自动样本 `dismount_20260726_081017_621` 已确认候选的所有前置门控实际命中，而不是
安装或会话匹配失败：

- RideOff OnEnter 内安装 action-request wrapper，并抑制动画 `state=4`；
- 同一 OnEnter 内抑制 mover action `request=1`，Enter 日志同时记录
  `animObserver=1 actionSuppressor=1`；
- 首次 RunPresentation 后于约 16ms 请求原生 operation 21 detach，并把 RideOff
  `next` 从 `3` 请求为 Free `0`；
- operation 21 日志后同一毫秒出现 `animation state requested=1 callbackScope=1`；
  结合已静态确认的终结器行为，这证明本轮原生 finalizer 已实际执行，而不只是启动时
  解析到了函数地址；
- 下车 CutIn 于约 31ms 正常 `Deactivate clean=1`，脚本确认原生 RideOff exit；
- 自动退出后同时记录 VehicleBoard 与主 DLL 的正常 `DLL_PROCESS_DETACH`。

`capture_manifest.csv` 给出的三个实际捕获中点为 125ms、320ms 和 711ms。125ms 帧中
玩家被车体遮挡，不能据此证明已经站到车外；320ms 帧明确显示玩家仍以跨出车辆的屈腿
姿态位于车体上，711ms 才接近车外站立。320ms 一帧已经直接否定“即时视觉跳过”，无需
再读取 1200/2000ms 帧。

因此，拦截 RideOff OnEnter 中已知的 `state=4` 与 `request=1`，再复用 operation 21
detach、原生终结器和 Free 状态退出，仍不足以阻止玩家视觉下车动作。该候选属于
`FLOW_PASS` 但视觉失败，不得作为快速下车实现保留，也不能据此恢复任何时间快进方案。
候选实现已经从源码撤回，回退构建输出 `BUILD_OK`；当时本地与游戏目录中的回退产物
均为 328192 字节，SHA-256 均为
`2B7BB51DE438BD7CB6210F0F711D0140640DA54CB7CFCAD60CA8062ED89ACFF5`。

### 2026-07-26 RideOff 提交前绕过的流程验证

在状态提交边界静态闭合后，运行样本
`dismount_20260726_084354_029` 验证了一个更窄的事实：仅包装
`DSPlayerRideVehicleActionPlugin` 的 pending-state 提交槽，并在
`current=2、pending=3` 时先请求原生 operation 21 detach、再把 pending 改为
Free `0`，能够让公共提交器直接完成 Drive 到 Free 的切换。日志在同一毫秒记录：

```text
FastRideOff pre-RideOff operation-21 detach requested current=2 next=3->0
FastRideOff pre-RideOff bypass complete current=0 next=0 flag=0
```

自动脚本同时确认安装日志存在，并断言本次下车起点之后没有
`RideOff Enter vtable original result=`；因此本轮确实没有执行 RideOff slot 11，
不是在 OnEnter 内或 RunPresentation 后再次加速。脚本正常完成退出并返回
`FLOW_PASS`。这一结果只确认 operation-21 请求和 Drive→Free 状态流程已经闭合；
`FLOW_PASS` 本身仍不代表玩家骨骼视觉已跳过。


提交前绕过的视觉和空间结论见
[FastVehicleRideOffRootMotionAndLanding.md](FastVehicleRideOffRootMotionAndLanding.md)。

本节中曾记录的 SHA-256
`2B7BB51DE438BD7CB6210F0F711D0140640DA54CB7CFCAD60CA8062ED89ACFF5`
只对应当时一次历史回退产物，已经被后续安全基线取代。当前部署基线以总览中的
`9123E64B3C1A2E5932A5DE5D333ACA8DF3B7E57C976EDB730B7819EC882B11B3`
为准。
