# 当前 V02 x64 可见页面首轮：外部 QEMU 中断

状态：**受外部负载中断，不计为 V02 交互通过**。本目录 `raw/` 保存 17 份原始驱动、启动、系统输入、截图、负载门禁与退出记录；`raw-manifest.json` 逐字节绑定外部原件，`review.json` 只给出限定判断。原始尝试名称为 `visible-v02-x64-attempt1`，后续不得覆写。

当前 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`，正式 x64 Release buildId `5ddcec93-d244-456c-877b-1bdeabee568d`，Workbench 完整载荷 SHA-256 `48ec1c2261bef39a45c2e95bff51b8313b50778f5d6ca4af51254fa88859c308`。监督器在空闲门禁后启动 `SouiWorkbench.exe --react --page=data`，原生 PID 57804。输入探针确认该进程的创建身份、可见窗口和自然前台；首次截图显示 V02 数据页与 10,000 行载入。一次真实系统滚轮输入后，第二张截图可见列表首行从 `fixture-00001` 变成 `fixture-00004`。这只是局部可见滚动观察，没有页面状态接口或时延结论。

随后外部 QEMU PID 78264 启动，运行监督器在 2026-09-27T11:00:50.896Z 发现外部负载并仅清理自己的 Workbench。外层驱动原始退出 1；应用原生退出 1 是强制清理结果，不是自然关闭通过或应用崩溃归因。后置构建复核因负载活动而延期。此次没有执行中文 IME 组合、任务并行、窗口缩放或完整原生关闭，`fullV02Verified=false`、`releaseReady=false`。待宿主再次空闲，用新尝试名称串行重测。
