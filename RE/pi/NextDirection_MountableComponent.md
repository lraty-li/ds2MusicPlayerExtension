# 下一方向：MountableComponent_StartMount

## 当前问题

rideKind 覆写不影响动画——因为动作参数已在 OnEnter 中写入完毕。
动画图读取的是参数包络（标量值）而非 rideKind。

## 唯一可行路径

NPC 货物使用 `MountableComponent_StartMount` 绕过 RideOnState：
1. NPC 货物无 RideVehicleActionPlugin
2. 无 RideOnState
3. 仅 `MountableComponent_StartMount → Entity_AttachToParentAndNotify` 附着
4. 无任何动画参数设置

## 玩家方案

在玩家上车时：
1. Hook `RideOnState_OnEnter`
2. **不调用原始函数**（跳过所有动画设置）
3. 从 vtable 读取 ProcessVehicleAttach 获取座椅实体
4. 找到车辆的 MountableComponent
5. 调用 `MountableComponent_StartMount(player, mountableComponent, mountPoint, slotTransform, 0)`
6. 设置 Drive 状态

## 需要 IDA 研究的

1. 从 `rideOn` 对象解析车辆实体的路径
2. 车辆实体 vtable+0x20 返回的 mountable side 对象地址
3. 从 seat 实体的 parent（+0x70？）找到车辆实体
