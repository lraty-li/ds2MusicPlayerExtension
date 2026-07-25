# 快速上车：DSVehicleTruck 机械座椅动画链

日期：2026-07-18

## 结论

正前方上车时出现的“玩家已经在驾驶座上，但驾驶座整体下降后又回升”不属于玩家
ActionGraph descriptor，也不属于 `DSCutInCamera`。它来自 `DSVehicleTruck` 自己的车辆
动画状态机：

```text
RideOnSeatState_ApplyApproachState
  -> truck.requestedAnimationState = 3 或 4
  -> DSVehicleTruck vtable slot 34/35 消费请求
  -> DSSimpleAnimationComponent::PlayState(3/4)
  -> 机械座椅动画开始
  -> RideOn OnExit 请求状态 0
  -> slot 34/35 消费状态 0
  -> 车辆动画回到基线
```

当前正前方自动测试实际选择状态 `3`。在此前的快速上车顺序中，状态 `3` 运行约
`0.10s` 后 RideOn 已进入 Drive，OnExit 随即请求状态 `0`。因此原本较长的“座椅下降、
玩家上车、座椅回升”被压成一个短促的下降/回位中间姿态，但没有被真正移除。

当前实现只在经过 RTTI、会话、当前状态和控制器播放状态联合验证后，于车辆消费前
取消 pending 的上车状态 `3..6`。车辆保持原生基线状态 `0`，常规车辆更新、玩家挂接、
ActionGraph、RideOn、CutInCamera 和 Drive 链仍分别按各自的正常接口完成。

## 调查方法

- 静态分析使用已知函数的定点反编译、反汇编、定向 xref 和限定范围指令查询。
- 通过 MSVC RTTI/COL 校验 `DSVehicleTruck` 与 `DSSimpleAnimationComponent` 的完整
  对象和子对象偏移。
- 运行时观察使用 RTTI 定位的可写 vtable slot wrapper，没有修改可执行代码字节。
- 正前方测试统一运行根目录 `test_boarding.ps1`，由脚本完成启动、上车、下车和退出。
- 功能测试同时录制桌面，在日志闭合后逐帧检查上车瞬间的座椅高度。

## DSVehicleTruck 对象与状态字段

`RideOnSeatState_ApplyApproachState` 的第一类 RTTI 分支已经确认是
`DSVehicleTruck`，而不是泛化的“seat object”。其主 vtable 的 COL offset 为 `0`。

已验证的尾部布局：

| 字段 | 类型 | 含义 |
|---|---|---|
| `truck+0x12F8` | `DSSimpleAnimationComponent*` | 车辆状态动画控制器 |
| `truck+0x1300` | `AnimationPlayback*` | 独立的车辆基础/参考 clip playback |
| `truck+0x1308` | 8 字节字段 | 语义尚未命名 |
| `truck+0x1310` | `int32_t` | `currentAnimationState` |
| `truck+0x1314` | `int32_t` | `requestedAnimationState`，空请求为 `-1` |

构造路径把 current 初始化为 `0`，request 初始化为 `-1`。`truck+0x1300` 是指针槽，
不是内嵌 playback；它与 `truck+0x1310/+0x1314` 不重叠。

## 方向到车辆动画状态的映射

`RideOnSeatState_ApplyApproachState` 对 `DSVehicleTruck` 的唯一车辆副作用是直接写
`truck+0x1314`。该分支没有调用 setter、发送车辆消息或同时修改其他 truck 字段。

| `ClassifyBoardingApproach` | 写入的请求状态 |
|---:|---:|
| `2` | `3` 或 `4`，由当前上下文字段选择 |
| `0` | `5` |
| `1` | `6` |

`DSVehicleTruck` vtable slot 36 只在 current 落入 `3..6` 时返回 true，因此 `3..6`
是同一组车辆侧上车动画状态。slot 37 对 `7..9` 返回 true；本轮自动下车也直接观察到
`0 -> 7`，与其属于下一组车辆动画状态一致。

## DSSimpleAnimationComponent

`truck+0x12F8` 的真实动态类型已经由构造路径、vfptr 和 COL 闭合为
`DSSimpleAnimationComponent`：

- primary COL offset `0`；
- secondary COL offset `32`；
- primary vtable slot 13 为 `DSSimpleAnimationComponent::PlayState`；
- `controller+0x50` 是实际的 `AnimationPlayback*`；
- 组件 Update 从 `updateContext+0x1C` 读取 delta，推进该 playback 并采样姿态。

控制器 playback 的 `+0x18` 在基线完成态为 `2`。`PlayState(3)` 后，本轮只读日志
观察到它从 `2` 变为 `1`，证明机械上车 clip 已进入活动播放态。

`truck+0x1300` 的独立 playback 在同一轮中始终保持 time、previousTime 和 lastDelta
为 `0`。静态更新链也证明它与 controller pose 是两条独立来源。因此不能通过推进
`truck+0x1300` 来完成状态 `3/4` 的机械座椅动作。

## 请求消费与正常回位

两个已验证的消费者为：

| vtable slot | 函数 |
|---:|---|
| `34` | `DSVehicleTruck_ConsumeAnimationRequestAndUpdate` |
| `35` | `DSVehicleTruck_ConsumeAnimationRequestAndUpdatePoseBuffers` |

二者逻辑 ABI 均为：

```cpp
void Function(DSVehicleTruck* truck, float deltaSeconds);
```

当 request 非负且与 current 不同时，消费者：

1. 调用 `DSSimpleAnimationComponent::PlayState(request, 2, 1.0, 0.0, 0.0)`；
2. 把 request 写入 current；
3. 把 request 清为 `-1`；
4. 继续执行原有车辆动画完成检查和车辆更新。

`DSPlayerVehicleRideOnState_OnExit` 调用
`RequestTruckPostRideOnAnimationState`。当 current 是 `3/4/5/6` 时，该 helper 请求
状态 `0`；current 为 `0` 时不写请求。状态 `0` 是构造、初始化和控制器初始化共同使用
的基线状态，不是插件伪造的禁用值。

## 只读基线运行证据

正前方只读测试中：

```text
绑定时：      current=0 request=3 controllerPlaybackState=2
下一车辆帧：  current=3 request=-1 controllerPlaybackState=1
约 0.100s：  RideOn 完成并进入 Drive
下一车辆帧：  current=3 request=0 -> current=0 request=-1
再下一帧：    controllerPlaybackState=2
```

`request=0` 的静态来源与运行时顺序一致：它由 RideOn OnExit 的卡车动画复位 helper
写入。由此闭合了用户所见短促升降的完整生命周期。

## 当前实现

生产实现位于 `TruckSeatTransitionObserver.cpp`，使用两层保护：

1. `RideOn ProcessVehicleAttach` 原函数返回后，从 `rideOn+0x98` 取得玩家，再从
   `player+0x80` 取得已挂接车辆。主 vfptr 与 RTTI 定位的 `DSVehicleTruck` primary
   vtable 不同的情况下，只有 slot 34/35 仍精确指向已验证的两个原实现时，才为该 vtable
   安装同一 wrapper；重写任一 slot 的未知类型不会被按 truck 布局处理。
2. 原始 primary vtable 与上述兼容 vtable 都由 slot 34/35 wrapper 在消费者调用原函数前
   再次执行同一会话与车辆校验，覆盖写入线程和消费线程之间的竞争窗口。

只有以下条件全部成立时才以原子 compare-exchange 把 request 从 `3..6` 改为 `-1`：

- 当前 RideOn 属于同一个五秒有界快速上车会话；
- 车辆是 `DSVehicleTruck` primary vtable，或 slot 34/35 复用已验证原实现的兼容 vtable；
- current 为原生基线状态 `0`；
- request 是已验证的车辆侧上车状态 `3`、`4`、`5` 或 `6`；
- `DSSimpleAnimationComponent` 和 controller playback 均存在；
- controller playback state 为原生完成值 `2`。

静态分析确认取消 pending 后只会跳过 `PlayState(3..6)` 和 current 写入；slot 34/35
后续的完成检查、pose 更新和完整车辆更新仍然执行。若字段读取、RTTI、会话、播放态
或 compare-exchange 任一项失败，机械座椅组件不会声明本次准备完成，其他快速上车层
也不会进入功能路径。

## 功能运行证据

最终正前方测试日志为：

```text
FastBoarding TruckSeat front request suppressed
session=1 current=0 request=3 playbackState=2
```

从取消请求到 Drive 期间没有出现 `current=3` 或 `current=4`。自动下车时仍正常出现：

```text
TruckSeat slot=34 state=0->7 => 7->-1 playbackState=2->1
```

同一二进制的完整 `test_boarding.ps1` 结果为 PASS，角色 descriptor、事件 186、Drive、
CutIn finished、CutIn Deactivate、下车和 DLL 卸载均正常。同步录屏逐帧显示：玩家从
车旁直接切换到保持升起基线的驾驶座，随后进入与车头同向的驾驶镜头；没有出现座椅
降低的中间帧。

验证产物：

- [正前方完整测试录屏](../artifacts/boarding/front_seat_fix_test.mp4)
- [上车窗口逐帧表](../artifacts/boarding/front_seat_fix_game_contact.png)

构建结果为 `0` warning、`0` error。新增实现文件为 `253` 个物理行，项目中所有代码
文件均满足不超过 `300` 行的限制。

最终实现还要求六个快速上车组件全部 ready 才允许取消 request。加入该失效保护后的
相同二进制完整重跑 PASS；最终日志再次确认 request `3` 在 current `0`、controller
playback state `2` 时被取消，且下车状态 `7`、CutIn 清理和 DLL 卸载正常。

## 装甲卡车侧面上车验证

2026-07-25 的实机部署日志验证了用户所说的“装甲版卡车侧面攀爬”路径。两个侧面
会话分别在原消费者执行前出现：

```text
session=1 current=0 request=5 playbackState=2
session=2 current=0 request=6 playbackState=2
```

两次 request 都由生产 wrapper 原子改为 `-1`，没有进入车辆机械上车 clip。对应的
玩家 descriptor 分别命中 leaf 1 和 leaf 4，均由 `timeScale=1` 加速到 `512`；原生
RideOn 完成事件在 elapsed `0.0417084s` 进入 Drive。两个 CutIn 分别由原 playback
推进至 finished（hash `0x53758BED`、`0x6F53F3A5`），并由原 Deactivate 清理完成。

因此该反馈的直接根因是旧实现只取消正前方 `3/4`，没有覆盖已验证的侧面上车状态
`5/6`；武器装备本身不是独立的生产判定条件。

## 已排除的错误路径

- 玩家 fullgame descriptor 只控制玩家实时演算，不控制卡车机械状态 `3/4`。
- `DSCutInCamera` 只控制 CutIn action 与相机交接，不控制卡车机械座椅。
- `truck+0x1300` 是独立基础/参考 playback，不是 controller 当前机械 clip。
- Coffin 车辆的 Drive mode `9` 属于另一车辆类型和字段，不是本链。
- `SeatController_StartOrFinishTransition(..., finishFlag=true)` 是另一条终态资源清理链；
  提前调用会清除仍被排队工作帧使用的 callback，并曾导致访问异常，不能用于完成本链。

## IDA 数据库维护

本轮按已验证行为更新了以下函数名、原型、参数名和关键控制流注释：

| 地址 | 当前名称 |
|---|---|
| `0x141F92770` | `DSVehicleTruck_ConsumeAnimationRequestAndUpdate` |
| `0x141F92800` | `DSVehicleTruck_ConsumeAnimationRequestAndUpdatePoseBuffers` |
| `0x141F8C930` | `DSVehicleTruck_AdvanceAnimationAndHandleStateCompletion` |
| `0x141F8C8E0` | `DSVehicleTruck_TransitionAnimationState` |
| `0x141F63010` | `VehicleEntity_CopyControllerPoseToInactiveBuffer` |
| `0x142684490` | `AnimationPlayback_AdvanceTime` |
| `0x142684320` | `AnimationPlayback_SetTime` |
| `0x14036F900` | `DSSimpleAnimationComponent_PlayState` |
| `0x14036DEF0` | `DSSimpleAnimationComponent_Update` |
| `0x141011600` | `RequestTruckPostRideOnAnimationState` |

数据库还增加了已闭合的 `AnimationPlayback` 字段类型、`DSVehicleTruck` 尾部字段类型、
`DSSimpleAnimationComponent` 主/次 vfptr 的 COL 说明，以及 request 消费、controller
playback 更新和状态完成回基线的证据性注释。未知字段仍保持未命名，没有按用途猜测。
