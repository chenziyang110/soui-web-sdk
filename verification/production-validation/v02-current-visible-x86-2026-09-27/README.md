# 当前 V02 x86 真实交互短测

本目录保留当前 718 文件 Demo 源码对应的正式 x86 Release Workbench，在自然前台 `--react --page=data` 下的一次真实系统输入。输入由已编译的 Win32 探针发送，逐次核对目标 PID、创建时间和窗口句柄；五张原始截图绑定同一窗口且与记录中的 SHA-256 一致。89 份原始文件列在 `raw-manifest.json`；`review.json` 区分机械核查和截图人工观察。

运行身份：源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`，正式构建 ID `f0c38056-058a-456e-be99-17446127f7ec`，Workbench 475 文件载荷 SHA-256 `7ee53c78a1c5c0c2deecfbb38f70d443d3315d6d420ec756c1b5113ccc7cbffb`；运行前后相同。外层驱动和应用原生退出码均为 0，前后外部负载门禁为空。

页面显示 10,000/10,000 行。八次外层滚轮抵达公共任务区；启动 30 秒任务后，真实拼音按键在页面提交“中文”，组合事件计数为 3。任务运行到 18% 时的截图同时显示汉字、进度事件/查询计数和内容区从 1100×720 拖拽到 1180×780。随后滚回列表顶部，任务启动后 15,568 ms 发送内层滚轮；前后截图首行从 1 变为 4，页面显示滚动距离 100 px。最后发送 `WM_CLOSE`，窗口销毁、进程自然退出 0，未强制终止。

这是 **x86 单轮可见交互短测**。输入探针的 `imeCompositionVerified=false` 和截图记录的 `pageStateVerified=false` 是其保守默认字段；上面的页面结论由人工核对原始窗口截图及输入、时间、身份记录得出。本次不测 Win7 SP1 x86、长期资源/输入时延/帧时间、DPI 多屏或 Electron 配对；`fullV02Verified=false`，`releaseReady=false`。此前受其他 QEMU 打断的 x86 尝试原件保存在独立的 interruptions 目录，不与本轮合并为隔离性能结论。
