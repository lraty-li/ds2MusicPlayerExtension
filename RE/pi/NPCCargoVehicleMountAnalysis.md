# NPC 货物车上安装机制

## 安装路径

```
PassengerCargo_UpdateMaybeVehicleMount (0x1408E2CD0)
  → PassengerCargo_CanStartVehicleMount (0x1408E6A90)
  → PassengerCargo_SelectSlotAndStartMount (0x1408E6F50)
    → MountableComponent_StartMount (0x1402F1EF0)
      → Entity_AttachToParentAndNotify (0x140130900)
```

`MountableComponent_StartMount` 不做任何动画设置——只存储安装点数据并将实体附着到父级。

## 安装侧解析

`PassengerCargo_SelectSlotAndStartMount` 从车辆实体获取可安装侧对象：

```asm
mov rax, [rcx+30h]     ; passengerCargo->container
mov rcx, [rax+2F0h]    ; container+0x2F0 = vehicle entity pointer
mov rax, [rcx]         ; vehicle vtable
call qword ptr [rax+20h] ; vehicle->vtable+0x20() → mountable side object
```

## 与玩家上车路径的差异

| 方面 | NPC 货物 | 玩家 |
|------|---------|------|
| 入口 | MountableComponent | RideVehicleActionPlugin → RideOnState |
| 动画设置 | 无 | ~40+ 参数包络 + 动画状态 5 |
| 座位控制器 | 无 | 过渡启动 + 完成 |
| 结果 | 瞬间出现在安装点 | 播放上车动画 |
