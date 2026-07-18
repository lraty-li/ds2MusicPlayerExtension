# Fast Vehicle Boarding Seat-Action Runtime Boundary

日期：2026-07-10

## 范围与方法

本记录基于三类证据：

1. 当前版本 IDA 对已知 RideOn、seat action、action params 函数的定点反编译与反汇编
2. `ds2_vehicle_boarding_trace` 的只读观测构建
3. `test_boarding.ps1` 驱动的左前方原版上车、下车、退出流程

没有使用全局查找，没有读取 `DS2.exe` 文件本身，没有改寄存器、角色 Transform
或游戏指令字节来改变上车行为。本轮 Hook 全部调用原函数。

> 当前状态（2026-07-18）：本文主体记录 2026-07-10 的历史只读/事件门控阶段，
> 其中“当前活跃观测面”仅指当时的 `v0.9.1`，不是现行生产构建。现行构建使用
> RideOn/Drive/Graph/CutIn/Mover/DSVehicleTruck 的 vtable 或可写函数指针槽 wrapper，
> 不安装本文列出的 JumpHook 探索入口。卡车机械座椅的当前闭合链见
> [FastVehicleBoardingTruckMechanicalAnimation.md](FastVehicleBoardingTruckMechanicalAnimation.md)。

## 2026-07-10 只读观测构建（历史基线）

当前 `Hooks.cpp` 不再安装以下实验性抑制器：

- `BoardingClipSuppressor`
- `PresentationSuppressor`
- fast gate / fast Drive 写入
- Drive 后 animation / pose cleanup

当前 `v0.9.1` 活跃观测面为：

- `DSPlayerVehicleRideOnState_ProcessVehicleAttach`
- `DriveEnter`
- `ActionParams_QueryBoolEventByParamId`
- `RideOnSeatState_ApplyApproachState`
- `SeatController_StartOrFinishTransition`
- `DSPlayerVehicleDriving_UpdateTransition`（只读且限定活动 RideOn callback）

所有活跃入口均通过模式匹配定位。`PatternScan::FindUnique` 会在 `.text` 中出现多重
命中时拒绝安装，不再接受“取第一个结果”。`SeatState` 的固定 RVA 和
`SeatTransition` 的固定调用返回 RVA 已移除。

## 2026-07-10 左前方原版时序

自动化流程完整通过，未崩溃：

```text
stage 0 -> 1  elapsed=0.00834168
stage 1 -> 2  elapsed=0.0166834
runtime+0x18B 0 -> 1  elapsed=3.24909
next 1 -> 2 / DriveEnter  elapsed=3.57859
```

这再次确认原版左前路径的顺序：

1. seat transition start
2. generic entity attach
3. approach seat state
4. stage 2 pose/filter 更新
5. live action 完成位触发 `owner+0x7378 bit24`
6. `ProcessVehicleAttach` 写 `runtime+0x18B=1`
7. `Update` 稍后进入正常 Drive 完成块

## Seat Action 的新证据

本轮解析到的 seat action 对象在整个 RideOn 阶段保持同一身份。左前路径的关键值为：

```text
seat action mode: 0，完成门之后转为 2
transition active: 1
approach state: current=5, requested=-1
total clips: 4
actual clip types: 6, 6, 6, 6
RideOn pose id: 24
pose request active: 1
```

因此下列旧方向已被当前运行证据否定：

- 左前长动画由 clip type `92/93/97/98` 之一直接驱动
- `SeatAction_HasTransitionProgressGate` 是左前上车的完成门
- pose id 在左前长动画过程中通过 `24..37` 多个值连续推进

在本轮中：

- `SeatAction_CountClipType(92/93/97/98)` 始终全部返回 `0`
- `SeatAction_HasTransitionProgressGate` 始终返回 `0`
- callback `+0x14` 始终为 `1`
- pose id 始终为 `24`

`SeatAction_HasTransitionProgressGate` 的静态语义仍然有效：它要求 transition active、
callback 存在、mode 为 `2`，再把 callback `+0x14` 与全局阈值比较。它参与某些
pose 分支，但不是本次左前路径在约 3.25 秒产生完成位的来源。

## Approach 字段不能混为一个方向枚举

同一次用户确认的左前方自动化路径同时出现：

```text
runtime kind=1
RideOnSeatState_ApplyApproachState argument=0
seat action current approach state=5
presentation hash（上一轮已验证）=0x53758BED
```

这证明 `runtime kind`、`ClassifyBoardingApproach` 返回值、seat action 状态和
presentation hash 是不同层级的数据，不能继续用单一 `0/1/2` 表统一解释三种动作。

## `0xED` Action 参数的当前版本纠正

当前 `DSPlayerVehicleRideOnState_Update` 在 `0x140F99FEC` 调用的是：

```text
0x140DBEA20 -> ActionParams_QueryBoolEventByParamId
```

该函数通过 action params 内部对象的 vtable `+0xE0` 查询布尔事件。旧记录使用的
相邻函数 `0x140DBE9A0` 走 vtable `+0xD8`，不是当前 Update 对参数 `237 (0xED)`
的调用目标。

`0xED` 返回 false 时，Update 继续检查运行时字节与 elapsed timer；返回 true 时也可
进入正常完成块。因此它是 Update 侧的完成输入之一，但它不等同于
`ProcessVehicleAttach` 更早消费的 `owner+0x7378 bit24`。

### 2026-07-10 事件门控脚本复测

修正自动化时序后，当前 helper 已运行命中：

```text
elapsed≈0.02s  ActionParamED result=0
elapsed=3.24909  owner7378=0x01000000，runtime+0x18B 0->1
elapsed=3.57859  ActionParamED result=1
elapsed=3.57859  next 1->2，进入 DriveEnter
```

这证明本次左前原版路径使用的是 action graph 的 `0xED` 正常完成信号，不是仅靠
elapsed timer fallback。`owner bit24` 与 `0xED` 是前后两个独立完成阶段：前者先让
`ProcessVehicleAttach` 完成 mount-side gate，后者约 0.33 秒后让 Update 进入 Drive。

`ProcessAttach` 调用前后都观察到 bit24 仍为 `1`，但下一次 pose/update 观测已经为
`0`。因此该位不是由 `ProcessVehicleAttach` 当场清除，而是在后续 action 更新帧中
复位。

## 自动化时序修复

旧 `test_boarding.ps1` 只使用固定等待：首次 `F` 未触发上车时，四秒后的第二次
`F` 仍会被当作下车输入；同时旧 `KeyScan` 在聚焦失败后仍发送按键。这两点会造成
菜单、恢复确认、上车和下车整体错位。

当前脚本改为事件门控：

1. 清理旧进程后再清日志
2. 确认 `VehicleBoard] hooks installed`
3. 聚焦失败时不发送输入，并最多重试三次
4. `F (BOARD)` 后必须观察到本次新增的 `ProcessAttach original stage 0->1`
5. 必须观察到 `DriveEnter exit` 才允许发送下车输入
6. 下车后必须观察到新的 `start=0 finishFlag=0` seat transition
7. 最后确认游戏进程实际退出

2026-07-10 事件门控复测一次通过，脚本输出：

```text
Confirmed RideOn stage 0->1
Confirmed DriveEnter
Confirmed seat transition finish
PASS: board, drive, dismount, and quit confirmed
```

## 崩溃分析记录

第二次自动化运行在载入阶段崩溃。没有回退到旧实验版本。

根因是：

1. 修正后的完整签名第一次真正命中 `SeatAction_HasTransitionProgressGate`
2. 旧 trampoline 覆盖了入口的条件相对跳转
3. trampoline 只复制指令，不重定位相对分支
4. 该函数在非上车流程也会被调用，错误跳转在载入阶段触发

修复方式是只覆盖首条完整的 7 字节比较指令，不把相对跳转复制进 trampoline；同时
在安装任何 seat-action Hook 前预检全部签名，避免签名失败后留下半安装状态。

修复后的 `test_boarding.ps1` 完整通过。

## 已验证的 mount-side / RideOn 完成链

人类货物路径仍然只在 `Entity_AttachToParentAndNotify` 与玩家 RideOn 汇合。
`MountableComponent_StartMount` 不包含玩家的 OnEnter 参数包络、seat pose request、
action-slot filters 与首次下车所需状态，不能作为玩家快速上车入口。

当前已闭合的 mount-side / RideOn 完成链为：

```text
type-6 seat action / action graph 产生 owner bit24
  -> 原 ProcessVehicleAttach 消费 bit24
  -> SeatTransition_StartHelper
  -> runtime+0x18B = 1
  -> 原 Update 正常完成块
  -> Drive
```

2026-07-10 的新增只读跟踪进一步排除了 `DSPlayerVehicleDriving` callback：其 update
在 `owner+0x7378 bit24` 已触发、`runtime+0x18B` 已变为 `1` 后才开始，且可见的
`callback+0x14` 从首次命中即为 `1`。它不是此前约三秒可见演出的时间轴所有者。

这条链只闭合玩家 mount-side 与 RideOn 状态，不包含独立的 `DSCutInCamera` 生命周期。
原先称为 presentation 的请求实际会启动 CutIn action；CutIn 以自己的
elapsed/duration 完成，并由 CameraMode 调用 `DSCutInCamera_Deactivate` 释放 target、
撤销相机副作用。进入 Drive 不等于 CutIn 已结束。

## IDA 数据库维护

本轮完成以下命名与类型修正：

- `DSPlayerVehicleRideOnState_Update`
- `SeatAction_CountClipType`
- `SeatAction_HasTransitionProgressGate`
- `ActionParams_QueryBoolEventByParamId`
- `SeatController_StartOrFinishTransition`

并在以下位置加入当前版本静态/运行注释：

- `0x140F9B2DA`
- `0x140F99FEC`
- `0x140F9A4F0`
- `0x140F9AD51`
- `0x141F6C0D0`
