目标 hook 游戏的音乐播放函数， 令游戏内播放器能够加载并播放我在某一目录放置的音乐文件
暂时以音乐替换为目标

Ds2MusicPlayerExtendLink ： 通往 dll 的 visual studio 工程 的快捷方式

Ds2MusicPlayerExtendLink\build.ps1 ： 构建脚本，发生代码更改时执行该脚本，只有失败的时候你才去自行构建，获取详细报错

任意单个代码文件不得超过300行，不得通过例如格式化代码，压缩换行等方式逃避该规则

设计或者实现代码方案时，不得使用任何知识库中“待验证”的知识作为确切依据，基于知识库提供方向但必须利用 ida mcp 分析出合理的依据例如得到明确的代码调用路径

“待验证记录” 不得作为任何推论的依据，不得作为任何形式的结论。 直到通过实际运行日志确认其观点

知识文件用中文写

实现新hook时，旧的不相关的hook直接去掉

读取文件用  UTF-8

知识库不是给你写流水账的！不允许编写例如“最新确认”或者 xx 时间日志发现，这种具有时效性的内容，如果发现有就必须按合理形式整理进入知识库且去掉原条目


## 子 agent 模型合规闸门
- 任何 IDA MCP 探索在创建子 agent 时，必须显式指定 `model=gpt-5.2` 与 `reasoning_effort=xhigh`
- 对于 IDA MCP 探索，不得为了使用某个 `agent_type` 而放松模型约束；只要某种创建方式存在把模型回退、覆写或展示成其他模型的风险，就禁止继续使用该方式

## 证据分级
- 任何结论必须明确属于以下三类之一：
  - 静态候选：只由 IDA 静态分析支持，尚未被运行日志命中
  - 运行已命中：已被当前目标操作流程中的日志命中，但仍未证明它就是最终替换边界
  - 已验证边界：既被运行日志命中，又证明该点同时关联到当前目标资源与实际字节流处理
- 只要不是“已验证边界”，就不得把某函数写成“真实主链已确认下一跳”“真正写回点”“可直接实施的替换点”
- 知识文件必须显式区分“静态候选”和“运行已命中”，禁止把候选边界写成既定事实

## 实现闸门
- 任何新的音频替换 hook，在实现前必须同时满足：
  - 当前目标操作流程里真实命中过
  - 能和当前目标资源稳定关联，关联键至少满足其一：`wemId`、`wemResource`、`segment`、`streamHandle`、运行时虚表槽位真实函数指针
  - 该点能拿到替换所需最小三元组中的足够信息：输出缓冲、写入长度、逻辑偏移/文件偏移
- 如果一个点只能证明“这是同类提交层”但拿不到输出缓冲，最多只允许把它当观测点，不得直接拿它做字节替换实现
- 如果一个点只能拿到输出缓冲但无法稳定关联到当前目标资源，也不得直接做替换实现

## 反证优先
- 只要某个静态候选点在完整目标流程里零命中一次，就必须降低其优先级，不得继续把它表述为“当前最可能主链”
- 如果：
  - hook 安装成功
  - 用户按既定流程完成一次完整操作
  - 仍然零命中
  则必须把这记为明确反证，并在知识库里修正原先表述
- 不允许用“可能装晚了”“可能线程错过了”这种解释无限续命；若要保留该解释，必须实现新的观测方案并用新日志证明

## 运行时优先于静态命名
- 对任何通过虚表或函数指针分发的链路，优先确认“运行时槽位实际指到了谁”，而不是优先相信当前静态命名函数就是目标
- 若静态分析得到的是虚调用边界，则下一轮首要任务应是记录：
  - 对应对象地址
  - 对应 vftable 地址
  - 对应槽位的运行时真实函数指针
- 只有当运行时真实函数指针与静态命名函数对上后，才允许把该静态函数名写入主结论

## 对象来源约束
- 对任何虚调用、函数指针调用、全局对象调用，未经“调用点实参来源 -> 运行时对象地址 -> 下一跳函数指针/虚表槽位”闭环，不得把某个静态全局、成员偏移或符号名写成真实对象来源
- 设计 hook 时，优先从实际调用点反推寄存器实参来源，不得先从静态全局或符号名反推调用点
- 若当前 hook 依赖某个对象来源假设，代码必须先记录：
  - 取值地址
  - 取到的原始值
  - 该值是否像合法指针
  - 下一跳指针或虚表值
- 只要对象来源本身仍是猜测，即使已经拿到部分字段，也不得直接进入正式替换实现

## 字段语义约束
- 对缓冲、长度、偏移、句柄、完成状态、回调上下文等字段，只有在 def-use 链或运行日志同时支持时，才允许使用语义命名
- 若只有形状相似、偏移相近或同类函数复用关系，不得把字段直接写成既定语义，必须保持中性命名或明确标注“静态候选”

## 完成路径约束
- 任何“完成函数”“释放函数”“轮询函数”在未被当前目标流程命中前，只能是静态候选，不得写成默认主路径
- 若 hook 安装成功且目标流程零命中一次，必须立刻降级该候选，不得继续默认它是主路径

## 观测失败归因
- 若动态 hook 没有安装日志、目标指针为零、目标值不是合法指针、或创建阶段早退，必须优先归类为“观测方案失败”，不得直接归类为“主链零命中”
- 只有在观测点已建立且用户完成完整目标操作后仍零命中，才能把该边界记为“当前主链反证”
- 若某个固定全局、成员偏移或对象取法在运行时读出明显非指针值、空 vftable 或空槽位，则必须把“该取法成立”记为明确反证，并停止继续围绕该取法推进

## 失败归因规则
- 当替换方案失败时，必须把失败拆成三层分别判断，不得笼统归因：
  - 目标曲目是否选中
  - 目标资源是否定位
  - 外部字节是否真正写入可听缓冲
- 只要日志里已经出现 `externalMusic armed`，就不得再把失败归因为“外部文件没有选中”
- 只要日志里已经出现稳定的 `requestFileId / wemId / segment` 绑定，就不得再把失败归因为“目标资源还没定位到”
- 在没看到覆写命中日志前，默认失败发生在“真实写回边界未命中”，而不是先怀疑外部文件选择层

## 方案收缩规则
- 同一轮只允许推进一个“主替换边界假设”
- 新假设一旦建立，旧的无关 hook 必须删除，避免多个未证实边界同时存在
- 每轮结束时必须明确写出：
  - 本轮新增的已验证事实
  - 本轮新增的反证
  - 下一轮唯一优先确认的运行时边界

## 工作循环
- ida 探索，探索相关函数，确定目标hook点
- 如果获得新知识，核对并补充到 “待验证记录”
- 实现hook，并调用 build.ps1 构建
- 用户启动游戏，进行对应的操作目标，得到输出日志
    - 用户会进行：将音乐库的第一、二首歌加入播放列表3，点击试听（此时光标位于第二首歌，会试听第二首歌），点击播放，点击下一首
- 日志分析阶段
- 结束日志分析阶段
- 根据分析结果，回到 ida 探索 

## 日志分析阶段
我会给你发送操作以及对应产生的日志， 我没说 “日志分析阶段结束” 就表示还没结束
这期间以分析日志为目标，不得进行hook实现
我将给你发送运行日志，你必须进行分析，归纳

每当 日志分析阶段结束
1：回顾本轮获取的新信息
2：核对知识文件，修正错误理论或者增加新知识
3：将已经获得验证，被更新到知识库的位于“待验证”的概念从“待验证”中移除避免膨胀
4：总结归纳本轮分析，思考下一轮计划，以获取更多信息以摸清音乐加载流程


## 角色提示
Your task is to create a complete and comprehensive reverse engineering analysis. Reference AGENTS.md to understand the project goals and ensure the analysis serves our purposes.

Use the following systematic methodology:

1. **Decompilation Analysis**
   - Thoroughly inspect the decompiler output
   - Add detailed comments documenting your findings
   - Focus on understanding the actual functionality and purpose of each component (do not rely on old, incorrect comments)

2. **Improve Readability in the Database**
   - Rename variables to sensible, descriptive names
   - Correct variable and argument types where necessary (especially pointers and array types)
   - Update function names to be descriptive of their actual purpose

3. **Deep Dive When Needed**
   - If more details are necessary, examine the disassembly and add comments with findings
   - Document any low-level behaviors that aren't clear from the decompilation alone
   - Use sub-agents to perform detailed analysis

4. **Important Constraints**
   - NEVER convert number bases yourself - use the int_convert MCP tool if needed
   - Use MCP tools to retrieve information as necessary
   - Derive all conclusions from actual analysis, not assumptions

5. **Documentation**
   - Produce comprehensive RE/*.md files with your findings
   - Document the steps taken and methodology used
   - When asked by the user, ensure accuracy over previous analysis file
   - Organize findings in a way that serves the project goals outlined in AGENTS.md or CLAUDE.md
