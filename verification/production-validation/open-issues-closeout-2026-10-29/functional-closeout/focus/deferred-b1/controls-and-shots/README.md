这是待 Root 审查的单一 b1 原生控制。a2 原始源码、失败证据及 SDK 均保持原样；未制作 ISO、编译或运行控制。

仅改焦点调用时序：保留原 WM_NULL 投递及菜单销毁，随后只投递一次 WM_APP+20。原回调的 returning 日志不声称已经退栈；只有原 GetMessage 循环实际取出 WM_NULL 并分派，再取出/分派完成消息、回调深度为0且 InSendMessageEx 为0，才确认退栈并继续。完成消息中重新检查原 owner/top-level/PID/TID/前台/ADD/V4/期限/清理条件。没有新激活、计时器、重试或队列循环。

保持 id19、NID976、V4、隐藏顶层 owner、真实右键菜单/Esc外部证据、单次普通前台尝试及单次 NIM_SETFOCUS。内层60000ms、外层90000ms、drain2000ms不变。FALSE依旧exit1，非法/超时/清理失败依旧exit2；自然关闭不等于测试成功。未知出生时间的Node控制器仍要求PID当前不存在，根进程仍要求真实PID/出生时间和整个Job关闭后才能导出。

6个平面输入共67139字节在 ControlIso.selection.prepared.json。两个控制源码是 NativeFocusControl.cs 与 Invoke-NativeFocusControl-b1.ps1；Owned/export仅替换b1文件名、证据名及准备摘要，Loader-Crt.inputs.json完全复用。现有 Decode-FocusExport.cjs 已允许 focusc-b1-run，无需改解码守卫。

Root 审查固定来源后，未来仅在本任务VM的 D: 介质运行 C:/issues8/tools/node.exe D:/Focus-Owned-b1.cjs。内层新目录为 C:/issue-closeout-evidence/focusc-b1，外层为 focusc-b1-run；两者存在时拒绝复用。必须只做一次真实托盘右键/Esc并保留外部观察。未来导出使用同介质 FocusExport-b1.ps1，并由现有解码器仅取实际生成的精确标签。此处没有执行这些步骤。

静态检查：两个PS文件语法零错误、Owned JS通过node --check、9个纯路径正反例通过；C#未编译，仅源码审查。Interop新增 [InSendMessageEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-insendmessageex) 只记录当前过程是否正在处理其他线程发送的消息，不改变输入或焦点。
