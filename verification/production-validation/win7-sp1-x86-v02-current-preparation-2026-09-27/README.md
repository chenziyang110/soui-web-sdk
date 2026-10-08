# 当前 V02 Win7 SP1 x86 输入准备：载荷与探针

本目录保存当前 718 文件 Demo 源码对应的正式 x86 Release 运行载荷和 Win7 输入探针的**宿主准备证据**。35 份原始文件按 `raw-manifest.json` 索引；完整应用目录和 ZIP 保存在外部 `preview15-v02-win7-current-20260927/attempt1`，不重复提交大型二进制。本阶段归档时客体尚未启动，传输清单与客体脚本尚未冻结；后续预检另见相邻证据目录。

应用绑定源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`、构建 ID `f0c38056-058a-456e-be99-17446127f7ec`。Starter 71 文件、Workbench 475 文件分别匹配正式构建清单，各含 5 份法律声明；复制前后完整载荷校验通过。两份 ZIP 创建和测试均退出 0，散列及长度见 `raw/preparation/attempt1/payload-preparation.json`。宿主封装门禁无其他桌面或 QEMU 负载，驱动退出 0。

输入探针从已归档的 Win32 源码独立构建 x86 Release，使用静态 MSVC 运行库；配置和编译原始退出 0。`dumpbin` 记录该 EXE 的直接 DLL 依赖只有 `USER32.dll` 和 `KERNEL32.dll`，PE 子系统版本为 6.00；精确 SHA-256 为 `3d7b983d9e816978748b3ea2b3fa40a0ad004ab18c01982e5c2736e71b274737`。这些宿主检查只是客体兼容性前置条件，**不证明探针或应用已在 Win7 上运行**。原始编译警告保留，不以编译退出 0 消除风险。

下一步需新建并冻结 Win7 PowerShell 2.0 可运行的传输及客体脚本，在快照客体确认真实桌面后只发送一次命令，复核客体身份、资源/legal、输入与截图、串口和应用自然退出。旧 717 文件 Demo 的 Win7 三入口短测不能继承给当前输入。本目录 `guestExecuted=false`、`win7V02Verified=false`、`releaseReady=false`。
