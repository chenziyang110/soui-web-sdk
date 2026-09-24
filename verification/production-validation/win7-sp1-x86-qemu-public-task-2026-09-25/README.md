# Win7 SP1 x86 公开 SDK 任务客体准备

状态：**已准备，未执行来宾测试**。本目录记录待审查 PR #26 的公开任务验收包，不代表 Win7、Demo 页面或生产能力通过；`releaseReady=false`。

输入是当前分支完成双架构构建的独立安装消费者。x86 suite：`d3543a00-97f0-4110-b3f8-b44b8a86da49`；源码指纹 `0d440d307ba133250b675eac1f6e4b2edf7452e65277e84d8c3c62e621e4b913`；消费者 build-id `b8f87bb4-b2ff-402e-ab71-a74c2693b421`；75 个运行文件载荷指纹 `f51bff4cbfa9147bd3310aaa4dbb27b58cecc7813196e196eda344bc1c4dbeaa`。逐文件核对后制作只读客体传输 ZIP，`7z t` 完整性通过。ZIP、EXE、内核、SOUI DLL 和 7za 的 SHA-256 在 [input.json](input.json) 中固定。

宿主 x86 preview.5 八小时诊断结束后，且无其他验收进程时，执行 [QEMU 启动脚本](harness/launch-public-task.ps1)。脚本检查冻结输入、宿主终态和并发进程，以 QEMU `-snapshot`、只读 FAT 传输盘和无网卡启动已授权 Windows 7 SP1 x86 客体。来宾在 NTFS 解包并运行 [Bridge 三轮脚本](harness/run-public-task.cmd)：页面必须实际完成公共 `task.start`、进度事件、结果、协作取消、Bridge 并发容量和错误恢复，才能回报就绪。客体经 COM1 导出结果；宿主用 [串口校验器](Verify-SerialExport.ps1)核对每个文件的长度、SHA-256、系统身份、ZIP/二进制身份及退出码，再用[生命周期校验器](Verify-Events.cjs)核对完整三轮步骤和资源采样。

这是单个虚拟机上的短场景，不替代物理 Win7 SP1 x86、100 次重载、8 小时业务负载、输入/DPI/多屏或正式发布门槛。若客体失败，应保存原始串口及日志，修复后换新的冻结输入，不能覆盖此记录。
