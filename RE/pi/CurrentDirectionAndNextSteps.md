# 当前状态和下一步方向

## 已完成的分析

### 关键发现

1. **参数包络在角色实体上**（rideOn+0x98），不是在独立的内存块中
2. **OnEnter 的参数写入模式**：flag byte（+0x04 表示活跃）+ float value 对
3. **Decima 动画图直接从角色实体字段读取参数**，独立于 C++ 状态机
4. **当前已确认的 C++ 边界**：图不直接读取 inner+0x2E0（动画状态）、inner+0x544（相位）或状态机字节

### 当前实现的方案 (v0.14.0)

**参数包络恢复方案**：
1. Hook `OnEnter`（sub_140F98D00）
2. 调用前：保存角色实体上关键的参数值（+0x3740, +0x3770, +0x3950, +0x3DD0 和所有 flag 字节）
3. 调用原始 OnEnter（获取必要副作用）
4. 调用后：恢复所有保存的参数值
5. 继续使用现有的 FastDrive（ProcessVehicleAttach gate forcing → Update Drive transition）

### 下一步方向

如果参数包络恢复**不工作**（动画仍在播放），下一步：

1. **检查是否动画图在其他系统**：可能除了角色实体参数外，图还读取其他数据源的参数
2. **尝试更激进的方式**：在 OnEnter 后将角色实体的关键参数清理为 0（不清除 flag，直接清值）
3. **反向方案**：不调用原始 OnEnter 并使用 `MountableComponent_StartMount` 直接挂载玩家到座位上，然后手动补 RideOn 参数设置
4. **游戏日志分析**：检查 ds2_dll_music_resource.log 和 log.txt 中的错误信息

如果在调用原始 OnEnter 后立即崩溃：

1. **检查 charEntity 指针是否有效**：rideOn+0x98 可能在不同上下文中指向不同对象
2. **检查参数偏移是否随版本变化**：需要验证当前版本是否与签名匹配

## 引用

- 参数包络详细映射：`ParameterEnvelopeAnalysis.md`
- 上车方向分析：`VerifiedFactsAndBoundary.md`
