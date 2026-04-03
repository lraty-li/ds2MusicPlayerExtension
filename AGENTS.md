目标 hook 游戏的音乐播放函数， 令游戏内播放器能够加载并播放我在某一目录放置的音乐文件
我反复强调强调我的目标是加载额外的音乐，我不关心播放器的真正实现， 不关心如何切歌或者播放下一首的具体实现

Ds2MusicPlayerExtendLink ： 通往 dll 的 visual studio 工程 的快捷方式

Ds2MusicPlayerExtendLink\build.ps1 ： 构建脚本，发生代码更改时执行该脚本，只有失败的时候你才去自行构建，获取详细报错

任意单个代码文件不得超过300行，不得通过例如格式化代码，压缩换行等方式逃避该规则

设计或者实现代码方案时，不得使用任何知识库的知识作为确切依据，基于知识库提供方向但必须利用 ida mcp 分析出合理的依据

“待验证记录” 不得作为任何推论的依据，不得作为任何形式的结论。 直到通过实际运行日志确认其观点

知识文件用中文写

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
3：总结归纳本轮分析，思考下一轮计划，以获取更多信息以摸清音乐加载流程


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