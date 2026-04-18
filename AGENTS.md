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

没有我的明确允许，不准读取仓库文件

## 子 agent 模型合规闸门
- 不准产生同时多个agent同时调用 ida mcp 的情况
- 任何 IDA MCP 探索在创建子 agent 时，必须显式指定 `model=gpt-5.2` 与 `reasoning_effort=xhigh`
- 对于 IDA MCP 探索，不得为了使用某个 `agent_type` 而放松模型约束；只要某种创建方式存在把模型回退、覆写或展示成其他模型的风险，就禁止继续使用该方式


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
