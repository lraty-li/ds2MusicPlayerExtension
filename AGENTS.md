build.ps1 ： 构建脚本，发生代码更改时执行该脚本，只有失败的时候你才去自行构建，获取详细报错

任意单个代码文件不得超过300行，不得通过例如格式化代码，压缩换行等方式逃避该规则

读取文件用  UTF-8

不要并发调用 ida mcp

不准调用 survey_binary，find_regex， ，find，search_text。 不准调用任何全局查找的工具

关于 ida 调用不需要问我，不需要获得许可，直接进行

不准往知识文档写待定方案

让用户启动游戏是非常昂贵的事情，尽可能通过 ida mcp 调查

不准使用 dumpbin.exe 去读取 DS2.exe 本身， 不准以任何形式去读取 DS2.exe 本身

如果有任何 ida mcp 调用超时，就说明你进行了一个非常复杂的操作，必须等到 ida mcp 响应。不得继续调用导致 ida 直到卡死

没有我的允许，不得操作 git stage 命令

游戏日志：
[text](<f:/SteamLibrary/steamapps/common/DEATH STRANDING 2 - ON THE BEACH/ds2_dll_music_resource.log>)
[text](<f:/SteamLibrary/steamapps/common/DEATH STRANDING 2 - ON THE BEACH/log.txt>)

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
