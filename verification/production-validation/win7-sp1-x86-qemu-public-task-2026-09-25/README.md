# Win7 SP1 x86 公开 SDK 任务客体准备

状态：**Win7 SP1 x86 QEMU 的公开任务短测已通过**。本目录记录待审查 PR #26 的公开任务验收包；这不代表 Demo 页面、长期资源或生产能力通过，`releaseReady=false`。

输入是当前分支完成双架构构建的独立安装消费者。x86 suite：`d3543a00-97f0-4110-b3f8-b44b8a86da49`；源码指纹 `0d440d307ba133250b675eac1f6e4b2edf7452e65277e84d8c3c62e621e4b913`；消费者 build-id `b8f87bb4-b2ff-402e-ab71-a74c2693b421`；75 个运行文件载荷指纹 `f51bff4cbfa9147bd3310aaa4dbb27b58cecc7813196e196eda344bc1c4dbeaa`。逐文件核对后制作只读客体传输 ZIP，`7z t` 完整性通过。ZIP、EXE、内核、SOUI DLL 和 7za 的 SHA-256 在 [input.json](input.json) 中固定。

宿主 x86 preview.5 八小时诊断结束后，且无其他验收进程时，执行 [QEMU 启动脚本](harness/launch-public-task.ps1)。脚本检查冻结输入、宿主终态和并发进程，以 QEMU `-snapshot`、只读 FAT 传输盘和无网卡启动已授权 Windows 7 SP1 x86 客体。启动本身不发送按键；[桌面截图](evidence/boot-desktop.png)确认已就绪后，[一次性命令发送记录](evidence/key-send.json)固定实际操作。来宾在 NTFS 解包并运行 [Bridge 三轮脚本](harness/run-public-task.cmd)：页面实际完成公共 `task.start`、进度事件、结果、协作取消、Bridge 并发容量和错误恢复后回报就绪。客体经 [COM1 原始帧](serial-export.txt)导出 14 个结果；宿主[串口校验器](Verify-SerialExport.ps1)核对每个文件的长度、SHA-256、系统身份、ZIP/二进制身份及退出码，生成[验证记录](results/verification.json)，再由[生命周期校验器](Verify-Events.cjs)核对三轮、10 个完整步骤和资源采样。[校验输出](evidence/events-verifier-output.txt)与[终态画面](evidence/complete-console.png)均保留，QEMU 已退出，基础镜像未写回。

这是单个虚拟机上的短场景，不替代物理 Win7 SP1 x86、100 次重载、8 小时业务负载、输入/DPI/多屏或正式发布门槛。若客体失败，应保存原始串口及日志，修复后换新的冻结输入，不能覆盖此记录。
