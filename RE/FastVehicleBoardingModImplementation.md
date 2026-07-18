# 玩家快速上车 Mod 实现与验证

日期：2026-07-18

## 当前结果

`ds2_vehicle_boarding_trace` 已实现保留原生状态语义的快速上车：

- 原生 `RideOn OnEnter`、seat transition、玩家实体挂接和 stage `0 -> 1 -> 2`
  全部照常执行；
- 活动的 fullgame 上车 descriptor 由原 evaluator 以 `timeScale=64` 多帧推进到
  正常末端；
- 原生 ActionGraph 完成事件进入 RideOn 公共完成块，正常请求 `RideOn -> Drive`；
- Drive 进入后，等待 `DSPlayerMoverAccessor::OnModifyAnimatedPose` 把玩家 world basis
  提交为驾驶姿态；
- 随后才重复调用原生 `DSCutInCamera` playback，让原函数设置 finished；
- CameraMode 再调用原生 `DSCutInCamera_Deactivate`，完成镜头交接并释放所有 CutIn
  副作用；
- 原生下车、菜单退出和 DLL 卸载保持正常。

正前方自动测试的 `200ms`、`700ms`、`1700ms` 截图均显示车辆后方、与车头同向的
驾驶镜头。旧版本中正前方上车后垂直看地的问题不再出现。

实现没有修改寄存器、可执行代码字节或使用固定 RVA 构造 hook 目标。DS2 类函数入口
通过唯一模式或精确 RTTI/COL 定位；fullgame 调用点通过唯一模式定位并交叉校验到同一
可写 evaluator 函数指针槽。任一必需组件定位失败时，required-component mask 不成立，
功能 wrapper 只调用原函数。

## 必需组件与会话边界

当前 required-component mask 包含五层：

1. RideOn Enter、ProcessVehicleAttach、Update 与 Drive Enter 的状态范围；
2. `GraphAnimationManager` bool-event 查询；
3. `DSCutInCamera` playback / Deactivate；
4. fullgame descriptor evaluator；
5. `DSPlayerMoverAccessor` 的 ModifyAnimatedPose 提交点。

`FastBoardingSession` 的运行边界为：

1. RideOn vtable slot 11 原生 OnEnter 返回后，记录 RideOn、plugin、玩家实体和
   `GraphAnimationManager`；
2. slot 27 原生 ProcessVehicleAttach 返回后，只在
   `current=1,next=1,stage=2,b189=1,b18A=1,b191=1` 时进入 ready；
3. RideOn Update slot 14 用线程局部范围限定事件查询；
4. 事件放行前再次验证同一 manager、stage 2 和 `b18B=1`；
5. Drive Enter 只接受同一 plugin 的状态边界；
6. ModifyAnimatedPose 只接受当前 RideOn 对应玩家，并在原 slot 返回后读取已经提交的
   world basis；
7. CutIn 实例和 action hash 必须与同一有界会话匹配。

会话窗口为 5 秒。超出窗口或快照不一致时，不再执行加速路径。

## 角色 descriptor 加速

fullgame 间接 evaluator 的已验证原型为：

```cpp
void EvaluateDescriptor(
    ActionGraphResult* output,
    Descriptor* descriptor,
    uint8_t mode,
    float timeScale,
    bool evaluateExtraChannels);
```

上车 leaf 的原生参数为 `(output, descriptor, 0, 1.0, true)`。DS2 evaluator 核心会
按第 4 参数缩放 descriptor 采样区间，并把输出 duration 写成
`descriptorDuration / timeScale`；第 5 参数控制附加结果通道，必须原样透传。

当前白名单包含生成图选择树中的四个 evaluator 返回点：

| approach | 生成图分支 | return RVA |
|---:|---|---:|
| `0` | 主分支 | `0x3607117` |
| `1` | side 0 | `0x3607B68` |
| `2` | composite 候选 A | `0x3607952` |
| `2` | composite 候选 C | `0x3607C1A` |

wrapper 不把一次求值强行伪造成完成。它把同一会话第一次实际命中的 leaf 与 descriptor
绑定，并在后续帧继续传入 `timeScale=64`，直到原 evaluator 自己产生合法的
`reachedEnd` 或 `syncDuration >= duration`。这样保留 count、items、single、引用所有权、
sync 和姿态通道的正常构造。

正前方运行直接确认：

```text
leaf=8 callerRva=0x3607C1A scale=1->64 mode=0 pose=1
duration=0.0550029 sync=0.0550029 end=1 complete=1
```

因此当前正前方路径使用 approach 2 composite 候选 C。该次 CutIn action hash 为
`0x3897A3D5`，selected variant index 为 `0`。

## RideOn 完成与 Drive 边界

`GraphAnimationManager` primary vtable slot 28 查询内部 bool event。RideOn 外部参数
`0xED` 映射到内部事件 ID `186`，context 为 0。

wrapper 只协调原生查询结果，不写 RideOn 的 next-state 字段。角色 descriptor 已完成、
原生事件已经出现且 `b18B=1` 后，事件 `186` 在原 RideOn Update 内返回 true；原函数
随即执行公共完成块并请求 `next=2`。Update 返回后记录该边界，Drive Enter 再由原生
状态机调用。

正前方验证时序为：

```text
descriptor complete                 RideOn elapsed≈0.083s
event 186 released                  RideOn elapsed≈0.100s
RideOn completion Update returned
Drive Enter                         RideOn elapsed≈0.100s
```

Drive Enter 返回后的快照为 `b18B=1,b191=1,b381=0x4`。

## 玩家姿态提交与正前方镜头根因

`DSPlayerState` 的 animated-pose 消息链已静态闭合：

```text
MsgPreModifyAnimatedPose
  -> DSPlayerMoverAccessor slot 0
  -> DSPlayerMover slot 49
  -> 更新 pose-motion 缓存

MsgModifyAnimatedPose
  -> DSPlayerMoverAccessor slot 1
  -> DSPlayerMover slot 50
  -> 在原调用返回前提交玩家 Entity world transform
```

两个 accessor 槽的逻辑 ABI 均为
`void(self, float frameDelta, AnimatedPoseWrapper* wrapper)`。当前 Mod 只包装精确 RTTI
定位的 slot 1，并始终先调用原函数。

正前方运行中，上车 descriptor 末端把玩家 world basis 推到明显倾斜状态；其第三行
末元素约为 `0.5708`。Drive Enter 本身没有立即改变该矩阵。Drive 后第一次 slot 1
仍提交旧的倾斜姿态，下一次 slot 1 才把矩阵恢复到近驾驶姿态：

```text
before M33=0.570845
after  M33=0.999797
```

`DSCutInCamera_Deactivate` 对本次 flags `0x40A14A0C` 命中 `0x200000` 分支且不命中
bit 0。该分支直接读取 `DSPlayerEntity+0x100` 的当前 world basis，把它与玩家相机
上下文方向组合后计算驾驶镜头 handoff yaw/pitch。

旧顺序在 world basis 仍倾斜时就完成 CutIn，原生 Deactivate 因而把倾斜姿态带进驾驶
镜头交接，表现为正前方上车后垂直看地。当前顺序先进入 Drive，等待原生 slot 1 提交
近驾驶姿态，再结束 CutIn；没有直接写玩家变换、相机角或 handoff 字段。

## CutIn 正常结束

`DSCutInCamera` 通过精确 RTTI、primary COL offset 0 和 vtable slot 9 定位。slot 9
原型为 `void(self, float frameDeltaSeconds)`。wrapper 先正常调用一次，并只在以下条件
全部成立时重复调用原 slot：

- 同一 RideOn 会话已经完成原生 completion Update 并进入 Drive；
- Drive 后的玩家姿态已经由 ModifyAnimatedPose 原生调用提交；
- action hash 属于静态闭合的十六个上车请求 hash；
- active、未 finished、variant 稳定；
- 首帧 handshake 已结束，没有末帧保持或 variant-advance flags；
- elapsed、duration 和每次推进量均为有限值，elapsed 严格前进；
- 更新次数受硬上限约束。

正前方运行中，原 slot 9 共执行 380 次更新，把 elapsed 从 `0.0875876` 推进到
`3.25744`；原生 duration 为 `3.25325`，最终由原函数设置 `finished=1`。

下一帧 `DSCutInCamera_Update` 返回 false，CameraMode 调用原 slot 5
`DSCutInCamera_Deactivate`。返回后的验证确认 active/finished、variant、hash、flags 和
switch-pending 均已清理。静态分析确认 Deactivate 同时释放 broker vehicle target、
related entity observers、runtime entries，并把控制权交给下一可用 CameraModule。

## 运行验证边界

用户已逐方向确认左前、右前和正前三种路径都能直接进入座位。左前与右前在此前版本
中已经得到与车头同向的驾驶镜头；正前方曾稳定复现垂直看地，并由上述顺序修复。

移除 AroundCamera、逐帧 basis 和 pose-channel 诊断后的生产构建，使用根目录
`test_boarding.ps1` 完整通过：

```text
PASS: fast boarding, Drive, dismount, and quit confirmed
```

自动截图 `drive_0200ms.png`、`drive_0700ms.png`、`drive_1700ms.png` 均为正常车辆
后方驾驶镜头；`dismount1_settled.png` 显示正常下车。该轮没有 CrashTrace，并以正常
`DLL_PROCESS_DETACH` 结束。

生产日志中的关键相对顺序为：Drive Enter 后约 `10ms` 出现
`post-Drive player pose committed`，约 `6ms` 后 CutIn 原生更新到 `finished=1`，再约
`9ms` 后原生 Deactivate 验证 `clean=1`。因此修复不依赖已移除的诊断 observer。

第一次同一修复二进制的退出菜单自动输入曾未成功命中，随后完全相同二进制重跑通过；
该次非复现失败发生在上车、Drive、镜头交接和下车均已成功之后，没有崩溃记录，属于
菜单输入自动化抖动，不能归因于 Mod 生命周期。
