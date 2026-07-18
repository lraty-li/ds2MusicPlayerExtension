# 当前 `ds2_vehicle_boarding_trace` 源码审计

> 2026-07-10：当前源码已切换为只读 seat-action 基线，本文的“当前实现”章节不再
> 代表工作区最新状态。最新证据见
> [FastVehicleBoardingSeatActionRuntime.md](FastVehicleBoardingSeatActionRuntime.md)。
>
> 2026-07-18：本文现仅作为 2026-07-09 源码历史审计保留。现行生产实现与完整验证见
> [FastVehicleBoardingModImplementation.md](FastVehicleBoardingModImplementation.md)；
> `DSVehicleTruck` 机械座椅链见
> [FastVehicleBoardingTruckMechanicalAnimation.md](FastVehicleBoardingTruckMechanicalAnimation.md)。

日期：2026-07-09

## 范围

本记录只基于以下三类证据：

1. 当前仓库中的 `ds2_vehicle_boarding_trace/RideOnEnterInterceptor.cpp`
2. 2026-07-09 使用 `test_boarding.ps1` 的自动化实测
3. 当前版本 IDA MCP 对已知函数的定点反编译与反汇编

未使用任何全局搜索工具。

## 当前源码实际在做什么

当前 `RideOnEnterInterceptor` 不是“保持原始上车链，再缩短完成门”的方案，
而是一个更激进、也更偏离当前已验证边界的实验分支：

1. Hook `DSPlayerVehicleRideOnState_OnEnter`
2. 不调用原始 `OnEnter`
3. 直接清 `rideOn+0x198` / `rideOn+0x180`
4. 直接写若干 `runtime` 标志位
5. 在 `OnEnter` 里重复调用 `DSPlayerVehicleRideOnState_ProcessVehicleAttach`
6. 直接置 `owner+0x7378` 的 bit 24
7. 直接把角色局部坐标写成 `(0.8, 0, 0)`
8. 最后直接 `return 0`

这意味着它既跳过了原始 `OnEnter` 的动作参数/动画初始化，也抢跑了
`ProcessVehicleAttach` 与 `Update` 的自然时序。

## 自动化测试结论

2026-07-09 23:27 使用 `test_boarding.ps1` 实测，游戏没有崩溃，但上车并未完成。

关键日志：

```text
=== HookOnEnter (skip + seat pos) ===
PA1
PA2
Gate
PA3
Pos (0.8, 0, 0)
Post: ... cur=0 next=1 ... stage=2 elapsed=0 b18A=1 b18B=0 ...
Drive rideKind=0
```

随后日志里没有出现 `DriveEnter`、`RideOnExit`、`RideOff` 等正常后续链路。

结论很直接：

- 当前源码把交互提前推进到了 `stage=2`
- 但没有让原始 live-completion 门把 `runtime+0x18B` 置为 `1`
- 同时也没有让原始状态机自然走到 `plugin+0x11A = 2`
- 所以表现就是“按 F 有反应，但没有真正上车”

这与用户观察到的“点击上车后实际上无法上车”完全一致。

## 与当前高可信边界的冲突

当前源码失败，不是因为“差一点就成功”，而是因为它绕开了已经验证过的
三个必要层级：

1. 原始 `OnEnter` 的参数包络与动作初始化
2. 原始 `ProcessVehicleAttach` 的时序推进
3. 原始 `Update` 在 `stage=2` 的 seated pose / action-slot filter 设置

它尤其有三个明显问题：

1. **`OnEnter` 直接返回**  
   这会切断原始 RideOn 初始化，而不是“缩短动画”。

2. **在 `OnEnter` 里重入 `ProcessVehicleAttach`**  
   这会把附着过程拉到错误时序，得到 `stage=2` 的表象，但拿不到真正完成门。

3. **直接写局部坐标不是座椅收敛**  
   这只是写变换，不等于座椅控制器、动作图、玩家状态三者都完成收敛。

## 当前版本静态核对

### 1. `DSPlayerVehicleRideOnState_OnEnter`：不是简单的“方向枚举写入器”

当前版本 `OnEnter` 地址：

- `0x140F98D00` -> `DSPlayerVehicleRideOnState_OnEnter`

在 `0x140F98F7C` 调用的 `sub_140157460`，经当前版本反汇编核对，
它是一个哈希表查找函数，不是一个简单的“前/左前/右前方向分类器”。

该调用附近的真实行为是：

1. 从动作/座位描述表里查一个描述项
2. 把结果写入 `runtime+0x2A0`
3. 读取描述项 `+0x40` 的浮点值
4. 把该值缩放后打包进后续消息/参数块

因此，当前版本里不能把 `0x140157460` 简化理解成
“只负责决定 rideKind 的函数”。

### 2. `DSPlayerVehicleRideOnState_Update`：先补玩家 seated pose/filter，再请求 Drive

当前版本 `Update` 地址：

- `0x140F99C60` -> `DSPlayerVehicleRideOnState_Update`

已核对到两个关键调用：

- `0x140F99E10` -> `RideOnState_UpdateSeatPoseRequest`
- `0x140F99E1E` -> `RideRuntime_SetRideOnActionSlotFilters`

这两个调用都发生在原始 `plugin+0x11A = 2` 之前。

原始 RideOn->Drive 请求写入点位于：

- `0x140F9A2E7` -> `mov word ptr [plugin+0x11A], 2`

也就是说，当前可见路径不是“先请求 Drive，再补 seated pose”，
而是：

1. `stage=2`
2. 先补玩家专属 seated pose request
3. 再补 RideOn action-slot filters
4. 满足晚期完成条件后，才写 `next_state = 2`

### 3. `DSPlayerVehicleRideOnState_ProcessVehicleAttach`：负责 attach 与 presentation 分流

当前版本 `ProcessVehicleAttach` 地址：

- `0x140F9A390` -> `DSPlayerVehicleRideOnState_ProcessVehicleAttach`

已核对的关键点：

- `0x140F9AD9D` 调用 `Entity_AttachToParentAndNotify`
- `0x140F9AC72` 附近会把 `runtime+0x189` 置位，并把 `rideOn+0x198` 推到 `2`
- `0x140F9AFCC` 会发送一次 presentation action request

这说明：

1. 玩家路径确实复用了通用 attach 原语  
   即 `Entity_AttachToParentAndNotify`

2. 但玩家路径并不等于 NPC/货物的 `MountableComponent_StartMount`

3. 前方 / 左前 / 右前 的分流，不只是一个裸状态位  
   `ProcessVehicleAttach` 会根据当前上下文选择不同的 presentation action hash

因此，想跳过长动画，不能只盯着“附着有没有完成”，还必须理解
`ProcessVehicleAttach` 里这一步如何把不同接近方向投递到 presentation/action 层。

## 当前实时演算过程的高可信模型

按当前版本静态核对，玩家上车的实时过程至少分三层：

### 第一层：`OnEnter`

- 初始化 RideOn 状态
- 读取动作/座位描述项
- 写入运行时字段与参数包络
- 为后续动画/动作图准备初始输入

### 第二层：`ProcessVehicleAttach`

- 处理座椅控制器过渡
- 推进 `stage 0 -> 1 -> 2`
- 执行通用 `Entity_AttachToParentAndNotify`
- 根据接近方向和上下文选择 presentation action

### 第三层：`Update`

- 在 `stage=2` 时补 seated pose request
- 补 action-slot filters
- 等待晚期完成门
- 最后才写 `plugin+0x11A = 2` 进入 Drive

这个模型直接解释了当前源码为什么失败：

- 它试图在第一层就越级做完第二层、第三层的事
- 结果只拿到了“stage 看起来像完成了”
- 没拿到真正的完成门与自然状态流转

## 关于“左前方上车动画没有变化”的当前结论

2026-07-09 23:43 的自动化左前方上车日志已经证明，当前测试脚本并没有落回“单一路径”：

```text
RideOnUpdate waiting ... kind=1 variant=1 stage=2 ...
ProcessAttach gate forced=1 ... kind=1 variant=1 stage=2 ...
FastDrive requested after live completion gate ... kind=1 variant=1 stage=2 ...
DriveEnter entry ... kind=1 variant=1 stage=2 ...
```

也就是说，左前方这条路径里的运行时分类字段仍然存在；问题不在
`Drive` 请求把方向分类覆盖成“统一上车”。

当前真正导致“可见上车动画没变化”的更可能原因，是
`DSPlayerVehicleRideOnState_ProcessVehicleAttach` 的调用顺序：

1. 先调用 `DSPlayerVehicleRideOnState_ClassifyBoardingApproach`
2. 再调用 `RideOnSeatState_ApplyApproachState`
3. 再在 `0x140F9AFCC` 调用全局 presentation request helper
4. 最后才回到 `RideOnState_Update`，由当前 fast path 请求 `next_state = 2`

因此，即使 fast path 已经在几十毫秒内进入 `DriveState`，
更早送出的 boarding presentation 仍然可能继续驱动可见上车表现。

本次源码已据此补上 `PresentationSuppressor`，并且不再只压
`0x53758BED`，而是同时压掉当前已确认的三条玩家上车 presentation hash：

- `0x53758BED`
- `0x6F53F3A5`
- `0x3897A3D5`

### 2026-07-09 23:53 suppressor 实测

本轮在完成 suppressor 接线后，再次运行 `test_boarding.ps1`。

关键日志：

```text
Presentation suppressed hash=0x53758bed target=0x45F11200000 force=0
RideOnUpdate waiting ... kind=1 variant=1 stage=2 ...
ProcessAttach gate forced=1 ... kind=1 variant=1 stage=2 ...
FastDrive requested after live completion gate ... kind=1 variant=1 stage=2 ...
DriveEnter entry ... kind=1 variant=1 stage=2 ...
DriveEnter exit ... b381=0x4
```

可以确认三件事：

1. 当前 suppressor 已实际命中玩家上车 presentation request
2. 命中 suppressor 之后，左前方测试路径仍然正常进入 `DriveEnter`
3. 左前方测试路径这次命中的 hash 是 `0x53758BED`

第三点非常关键。用户已明确说明：

- 车辆是右舵车
- 测试脚本固定从左前方上车

因此，旧记录里把 `0x53758BED` 推断为“车正前方路径”的结论，
当前已经不再可靠；至少在 2026-07-09 23:53 这次自动化实测中，
**左前方路径命中的就是 `0x53758BED`**。

更稳妥的当前结论应当改成：

- `0x53758BED`：当前自动化左前方路径
- `0x6F53F3A5` / `0x3897A3D5`：其余两条接近路径，仍需按当前版本重新逐条对位

### 2026-07-09 23:56 post-drive anim reset 实测

在保留上面的 presentation suppressor 基础上，本轮又把
`DriveEnter` 后的 RideOn anim component `vtable+0x20` 调用补回当前主实现，
直接请求 `state=1`。

关键日志：

```text
DriveEnter exit ... b381=0x4 animState 5->1
```

这说明：

1. 当前 fast path 在进入 `DriveState` 之后，确实还能直接驱动同一个
   RideOn inner animation object
2. 当前调用已经把 inner `+0x2E0` 从 `5` 改回了 `1`

因此，如果用户在这个版本上仍然观察到“自由镜头了，但上车动画没有变化”，
那么当前可见上车动作已经不能再简单归因于：

- global presentation request
- RideOn inner `state=5` 记录值

也就是说，真正还在驱动可见动作的更可能是：

- `RideOnState_UpdateSeatPoseRequest` 写入的 seat pose request
- seat action object 自身的 progress / clip family
- 或 `ProcessVehicleAttach` 期间写入的 approach-specific seat state

### 2026-07-09 23:58 post-drive pose request clear 实测

在上一轮 `animState 5->1` 基础上，本轮又在 `DriveEnter` 后对
`RideOnState_UpdateSeatPoseRequest` 生成的 pose owner 请求做了最小清理：

1. 读取当前 pose id
2. 将 `poseOwner+0x2104` 从 `1` 清为 `0`
3. 对 `poseOwner+0x2154` 再补一次 `0x1000` dirty bit

关键日志：

```text
DriveEnter exit ... animState 5->1 poseId=24 poseActive 1->0
```

这说明到目前为止，当前主实现已经在 `DriveEnter` 后同时完成了：

1. suppress boarding presentation request
2. RideOn inner anim state `5 -> 1`
3. pose request active `1 -> 0`

因此，如果用户在这个版本上仍然观察到“自由镜头，但上车动画仍未变化”，
那么剩余高嫌疑层级就进一步收缩为：

- seat action object 自身仍在推进的 clip/progress
- `ProcessVehicleAttach` 中 approach-specific seat state 写入后的座椅行为
- 或更底层的 seat controller transition 完成后仍保留的动作驱动

## 下一步方向

基于这次核对，下一步应该明确收敛到下面这条线：

1. **停止沿用当前 `OnEnter` 直接传送/直接重入 `ProcessVehicleAttach` 的源码分支**  
   这条线已经被实测证明会吞掉上车交互。

2. **保留原始 `OnEnter` 与原始 `Update(stage=2)` 两个玩家专属初始化层**  
   也就是保留：
   - `RideOnState_UpdateSeatPoseRequest`
   - `RideRuntime_SetRideOnActionSlotFilters`

3. **把真正的介入点继续收缩到 `ProcessVehicleAttach` 的晚期完成门**  
   目标不是“伪造 stage=2”，而是“更早满足导致 `runtime+0x18B=1` 的真实条件”。

4. **并行理解左前方路径对应的 presentation action 分流**  
   测试脚本固定从左前方上车，而当前版本 `ProcessVehicleAttach`
   明确会按上下文选择不同 action hash。  
   这比“猜一个 rideKind 整数然后硬改”更接近真实控制面。

明确不建议再作为主方向的路线：

- 在 `OnEnter` 里重复调用 `ProcessVehicleAttach`
- 直接写玩家局部坐标
- 直接把玩家套到 `MountableComponent_StartMount`
- 只改 `plugin+0x11A`
- 只改 `inner+0x2E0` / `inner+0x544`

## 2026-07-09 当前实现结果

在本次审计之后，`ds2_vehicle_boarding_trace` 已改为当前高可信边界实现：

1. **不再 Hook `OnEnter`**
2. 保留原始 `DSPlayerVehicleRideOnState_OnEnter`
3. Hook `DSPlayerVehicleRideOnState_ProcessVehicleAttach`
4. 仅在快照满足
   `cur=1 next=1 stage=2 b18A=1 b18B=0 b191=1`
   时，置 `owner+0x7378` bit24
5. Hook `DSPlayerVehicleRideOnState_Update`
6. 仅在原始 `Update` 返回后，且快照满足
   `cur=1 next=1 stage=2 b18A=1 b18B=1 b191=1`
   时写 `plugin+0x11A = 2`

为确认不是“只请求了 Drive 但没真正进入驾驶”，还额外加了
`DriveEnter` 观测 hook。

### 自动化验证

2026-07-09 23:37 再次使用 `test_boarding.ps1` 验证，关键日志如下：

```text
RideOnUpdate waiting for live completion gate ... kind=1 stage=2 elapsed=0.0291959 b18B=0
ProcessAttach gate forced=1 ... b18B 0->1 ...
FastDrive requested after live completion gate ... elapsed=0.0375375 b18B=1
DriveEnter entry ... cur=1 next=2 ...
DriveEnter exit  ... b381=0x4
```

这说明当前版本已经在左前方上车路径（`kind=1`）上完成了：

1. 保留原始玩家 RideOn 初始化
2. 更早满足 `ProcessVehicleAttach` 的晚期完成门
3. 在 `Update` 中按原始路径请求 `next_state = 2`
4. 真实进入 `DriveEnter`

结论：当前实现已经不再是“吞掉上车交互”的失败版本，而是一个
**当前版本可稳定进入 DriveState 的 fast boarding 实现**。

## 本次 IDA 数据库维护

本次已在 IDA 中完成以下命名：

- `DSPlayerVehicleRideOnState_OnEnter`
- `DSPlayerVehicleRideOnState_Update`
- `DSPlayerVehicleRideOnState_ProcessVehicleAttach`
- `RideOnState_UpdateSeatPoseRequest`
- `RideRuntime_SetRideOnActionSlotFilters`
- `MountableComponent_StartMount`
- `PassengerCargo_SelectSlotAndStartMount`
- `Entity_AttachToParentAndNotify`
- `SeatController_StartOrUpdateTransition`
- `SeatTransition_PreAttachHelper`
- `SeatTransition_StartHelper`

并在以下关键地址补充了注释：

- `0x140F98F7C`
- `0x140F99E10`
- `0x140F99E1E`
- `0x140F9A2E7`
- `0x140F9AC72`
- `0x140F9AD9D`
- `0x140F9AFCC`
