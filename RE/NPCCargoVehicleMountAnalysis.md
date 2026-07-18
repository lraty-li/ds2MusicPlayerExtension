# NPC 人类货物直接上车与玩家实时演算边界

## 文档范围

本文区分三套不能混为一谈的机制：

1. 作为货物运输的人类实体；
2. 具备 NPC 载具 Mover 的普通乘客；
3. 玩家 `DSPlayerVehicleRideOnState` 上车流程。

本文记录的 1.10 运行时版本为 `v1.10.89.0`，测试日期为 2026-07-17。运行时流程由 `test_boarding.ps1` 自动触发，测试位置为车辆左前方。

## 证据等级

- **1.10 运行时确认**：当前插件日志、RTTI/COL 校验和自动截图共同确认。
- **当前 fullgame.dll IDA 确认**：当前 IDA 实例只加载 `fullgame.dll`，用于动画图结果分析。
- **旧主模块静态反编译**：来自旧 IDB 的 `0x140...` 地址链。函数语义可参考，但绝对地址和当前版本函数边界不可复用。
- **归档名称证据**：来自 `archive/notes/names/all-names.txt`，用于确认类型、消息和资源名称，不等同于闭合调用链。

## 人类货物直接挂接链

旧主模块静态反编译得到以下调用链：

```text
PassengerCargo_UpdateMaybeVehicleMount (0x1408E2CD0)
  -> PassengerCargo_CanStartVehicleMount (0x1408E6A90)
  -> PassengerCargo_SelectSlotAndStartMount (0x1408E6F50)
  -> MountableComponent_StartMount (0x1402F1EF0)
  -> Entity_AttachToParentAndNotify (0x140130900)
```

该反编译中的 `MountableComponent_StartMount` 保存 mount point/slot 数据并执行父子实体挂接；函数内没有建立玩家 RideOn 动画参数包络，也没有选择上车动画结果。

`PassengerCargo_SelectSlotAndStartMount` 的旧反汇编还显示：它从货物容器取得车辆实体，再通过车辆虚调用取得可挂接侧对象，最后进入 `MountableComponent_StartMount`。这条链描述的是直接挂接原语，不是可以复用的“人类货物上车动画”。

这些 `0x140...` 地址属于旧主模块 IDB。当前 IDA 实例是独立的 `fullgame.dll`；把旧主模块 RVA 套到当前 fullgame 会落入无关的巨大聚合函数，因此本文不把它们标成 1.10 静态复核地址。

## 归档对 mount 语义的独立佐证

归档 RTTI/名称表包含以下消息和类型：

- `MsgStartMount`
- `MsgGetMountState`
- `MsgGetMountPosition`
- `MsgSetMountTransitionArrivalTime`
- `MsgRequestMountTransitionTimeTillAnimationEnd`
- `MsgGetInitialPose`
- `MsgStopMount`
- `MountTransitionMover`

`MountTransitionMover` 资源邻域包含 `SyncDisplacement`、`MountDestination`、`MountSpeed`、`JointIndex`、`AnimationState` 和 `SyncTimes`。这表明引擎把“到达挂接点”和“动画结束”建模为两个独立时点。

归档还存在：

- `DSNpcRideVehicleMover` 及其资源和回调表；
- `PassengerRB/RF/LB/LF` 的 hand、ride、helper 名称；
- 货物/存储位置枚举中的 `PassengerSeat`；
- 卡车资源邻域中的 `AliveHumanOn`、`AliveHumanOff` 和 `SeatJoint`。

其中 `DSNpcRideVehicleMover` 属于会走 Mover/过渡协议的 NPC 乘客层。`PassengerSeat` 证明人类货物可占用乘客座存储位。`AliveHumanOn/Off` 只是在卡车资源/声音名称邻域出现，目前没有闭合到直接挂接函数的调用链，不能把它们解释为动画函数。

## 玩家左前方上车的三个完成层

### 1. RideOn mount-arrival 层

主模块 `ProcessAttach` 通过以下运行时链定位：

```text
唯一函数签名
  -> .rdata 中唯一等值函数指针
  -> x64 Complete Object Locator pSelf 校验
  -> TypeDescriptor = .?AVDSPlayerVehicleRideOnState@@
  -> 主子对象 offset = 0
  -> vtable slot 27
```

该槽使用 vtable 数据指针观察器调用原函数，不修改执行代码、寄存器或对象状态。

同一张 `DSPlayerVehicleRideOnState` vtable 还确认了：

- slot 11：状态 `Enter`；
- slot 14：状态 `Update`；
- slot 27：`ProcessAttach`。

`RideOn::Enter` 原函数返回时，快照为 `current=0,next=RideOn(1),stage=0,elapsed=0`。第一次和第二次 `ProcessAttach` 分别在约 8ms 和 17ms 把 stage 推到 1、2；长 ActionGraph 描述符约 20ms 才第一次出现。因此动画结果不是在 `Enter` 返回前已经开始播放，而是在后续 stage 初始化完成后成为活动结果。

原生时间线：

| RideOn elapsed | 原生变化 |
|---:|---|
| 约 0.0125s | `stage 0 -> 1`，`runtime+0x18A` 变为 1 |
| 约 0.0209s | `stage 1 -> 2`，`runtime+0x189` 变为 1 |
| 3.24492s | `owner+0x7378` 出现 `0x01000000`，同次原调用令 `runtime+0x18B: 0 -> 1` |
| 下一次调用 | `owner+0x7378` 的该位已被消费清零 |

本次左前方测试的运行时参数为 `rideKind=1`、`rideVariant=1`。旧文档把某些 `rideKind` 当作“无动画入口”的结论不适用于本次 1.10 运行时事实。

### 2. ActionGraph/演出结果层

当前 `fullgame.dll` IDA 中，左前方测试选择动态状态表 key `0x0BC4A758`。该值是动画图状态哈希，不是已证明的方向枚举。

状态活动字节 `context+0x507BB` 控制描述符 `descriptorPack+0x2730`。间接描述符求值点为当前 IDA 地址 `0x183607111`。1.10 运行时结果为：

- `duration = 3.55355`
- `rangeStart/syncDuration` 从约 `0.00834` 按实时增长到约 `3.55`
- 结果具有有效 `single` 和引用所有权，不是单独的等待计时器
- 自动截图在 0.2s、0.7s、1.7s 分别显示俯身、攀入和踏板/座舱边缘姿态

同一间接求值槽的同步状态过滤和同线程/同帧局部过滤都只得到这个长结果本身。当前局部分支没有同时评估可直接替换的座椅/Drive 稳态结果。

### 3. 玩家状态机 Drive 层

`Drive::Enter` 通过同样的签名、`.rdata` 指针和 COL 链定位：

```text
TypeDescriptor = .?AVDSPlayerVehicleDriveState@@
主子对象 offset = 0
vtable slot 11
```

原生 `Drive::Enter` 在 RideOn elapsed `3.58276s` 调用。进入前状态为：

```text
current=RideOn(1)
next=Drive(2)
runtime+0x18B=1
runtime+0x191=0
```

原函数返回后 `runtime+0x191=1`、`runtime+0x381=0x4`。Drive 进入发生在 3.55355 秒动画结果自然结束之后，而不是在 3.24492 秒 mount-arrival 位出现时。

`RideOn::Update` 位于 vtable slot 14。原函数在一次 `delta=0.00834168` 的调用中把 `next: RideOn(1) -> Drive(2)`，并在同一日志时间进入 `Drive::Enter`。相邻两次 Update 之间，plugin 前 `0x400` 字节只有两个对齐 32 位值发生变化：

- `plugin+0x1AC`: float `3.56607 -> 3.57442`，与 RideOn elapsed 同步；
- `plugin+0x31C`: float `-0.321154 -> -0.329496`，按帧连续递减。

RideOn 对象前 `0x200` 字节在这两个相邻 Update 之间没有变化，也没有新的本地布尔完成位翻转。自然 `next=Drive` 请求因此不是由另一个可见的 RideOn 本地 flag 单独触发；该 Update 消费的是连续进度和/或对象外部的演出完成查询结果。

## 已排除的错误等价关系

以下三件事不等价：

```text
实体已挂接/到达座位
!= ActionGraph 上车结果已结束
!= 玩家状态机已经完成 Drive 初始化
```

旧实验提前写入 `owner+0x7378` 完成位，并提前请求 `next=Drive`，只伪造了第一层和部分第三层。3.55355 秒的 ActionGraph 结果仍保持活动，所以会出现“玩家已经在车上，但镜头动画和等待时长仍在”的分层失配。

## 自动化验证结果

`test_boarding.ps1` 已连续验证：

- `DSPlayerVehicleRideOnState` slot 27 vtable 观察器安装成功；
- `DSPlayerVehicleDriveState` slot 11 vtable 观察器安装成功；
- 左前方上车描述符自然开始并在约 3.55 秒结束；
- Drive 镜头在结果结束后恢复；
- 原生下车完成；
- 游戏内退出完成；
- 全流程无崩溃。

观察器地址均由当前版本函数签名、唯一 vtable 指针匹配和 RTTI/COL 校验确定，没有使用固定模块偏移。
