# 上车动画跳过研究总结

## 已确认边界

### 1. 参数包络恢复 (v0.14.0, v0.17.0)
- **方法**：在 OnEnter 后恢复角色实体的参数包络值
- **已确认边界**：当前可见动画图的直接输入不等同于这组角色实体参数包络

### 2. 动画轨道清除 + 状态重置 (v0.16.0)
- **方法**：清除 inner+0x28 的轨道槽标记 + 重置 inner+0x2E0=1
- **已确认边界**：当前可见动画图的驱动面不止于 inner 动画对象的轨道槽与状态字段

### 3. 状态机加速 Drive (v0.13-v0.17)
- **方法**：立即写入 plugin+0x11A=2 强制 Drive 状态
- **已确认边界**：状态机流转可提前进入 Drive，但已加载的上车剪辑生命周期独立存在

### 4. Handler 表重定向 RideOn→Drive (v0.18.0)
- **方法**：修改 handler 表 funcs_140FE45CA[1]=Drive handler
- **已确认边界**：仅重定向状态处理器不足以补齐玩家附着、位置与姿态初始化

### 5. 跳过 OnEnter + ProcessVehicleAttach (v0.15.0, v0.20.0)
- **方法**：不调用原始 OnEnter，手动调用 ProcessVehicleAttach×2
- **已确认边界**：ProcessVehicleAttach 负责附着，但不单独承担把玩家收敛到座椅最终空间位置的全部工作

## 核心问题

**Decima 动画图**是一个可视化脚本系统，不是 C++ 动画组件的简单扩展。它：
1. **不从 inner+0x2E0（动画状态）读取**——已验证
2. **不从 inner+0x544（相位）读取**——已验证
3. **不从 inner+0x28 轨道槽读取**——已验证
4. **不从状态机字节 (0x118/0x11A) 读取**——已验证  
5. **不从角色实体参数包络字段读取**——v0.14.0/v0.17.0 验证

**它从 ActionTree（动作树）系统读取。** ActionTree 是 Decima 引擎管理动画动作的系统。
OnEnter 调用的 `MsgDsBaggageEvent` 激活了 ActionTree 中的 RideVehicle 动作，其动画图随后独立运行。

**"原地坐下"的根因**：`Entity_AttachToParentAndNotify` 只改变父级关系，不改变世界位置。
没有上车动画来过渡位置，玩家保持在原地。

## 下一探索方向

### 方向A：理解 ActionTree 参数系统
需要使用 IDA 深入调查 RideVehicle ActionTree 的输入参数和数据源。
关键目标：找到 ActionTree 接收哪些输入来控制动画图。

### 方向B：使用 MountableComponent_StartMount
NPC/货物使用此函数直接挂载到座位上，完全绕过 RideVehicle ActionTree。
玩家如果也能使用此路径，则可以完全跳过上车动画。
需要解决的问题：
1. 从 rideOn 上下文找到车辆实体的 MountableComponent
2. 提供正确的 mountPoint 和 slotTransform
3. 设置 Drive 状态

### 方向C：手动设置玩家位置
在 skip-OnEnter 方法中，在 ProcessVehicleAttach 后将玩家实体位置设置为座位位置。
需要知道 Decima 实体位置设置函数。
