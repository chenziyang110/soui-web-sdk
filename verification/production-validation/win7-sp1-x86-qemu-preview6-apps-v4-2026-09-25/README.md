# preview.6 Win7 SP1 x86 应用短测 v4

状态：**Win7 SP1 x86 QEMU 的三项应用短测已通过**，但 `releaseReady=false`。v3 [真实来宾测试](../win7-sp1-x86-qemu-preview6-apps-2026-09-25/failed-v3/README.md)确认两个 ZIP 在 Win7 解压并保留中文文件名，但来宾 PowerShell 2 在 `SHA256Managed.Dispose()` 处失败，三次应用 smoke 没有开始。v4 只将该来宾校验器改用 .NET `HashAlgorithm.Clear()`；Starter 与 Workbench 的构建载荷、EXE、`node.dll`、`soui.dll` 指纹仍与 v3 相同，重新压缩后的 ZIP 和脚本散列在 [input.json](input.json) 中独立冻结。

新传输目录为 `D:\SOUI-SDK-QEMU-validation\guest-preview6-app-transfer-20260925-v4`。准备脚本逐文件核对两个构建载荷，测试 ZIP、重新解压并验证 71/475 个文件。宿主使用实际解压目录运行来宾校验器，通过中文夹具及 10 份法务文件散列检查；PowerShell 脚本语法无错误，串口校验器 24 文件成功帧及篡改内核散列的失败帧自检通过。

2026-09-25 07:58 启动 [QEMU](evidence/launch.json)，[桌面截图](evidence/boot-desktop.png)确认 Win7 桌面后只[发送一次命令](evidence/key-send.json)。来宾从只读传输盘在 C: NTFS 解包，完成许可/载荷检查与 Starter、Workbench 默认页、React 页三项 smoke，再经 [原始 COM1 帧](serial-export.txt)回传。[串口校验器输出](evidence/verifier-output.txt)确认 **24/24 文件**、Win7 SP1 x86 身份、冻结 ZIP/EXE/内核/SOUI DLL 散列、许可文件散列、三项退出码均为 0；[终态画面](evidence/complete-console.png)也显示三项 0 退出。原始 [Starter](results/starter-smoke.log)、[Workbench 默认页](results/workbench-default-smoke.log)和 [React 页](results/workbench-react-smoke.log)日志记录应用就绪与 Host/窗口关闭终点。该 QEMU 使用 `-snapshot`、只读 FAT 传输盘和无网卡，已退出且未写回基础镜像。

本场景仅覆盖三项短启动及关闭路径，不覆盖真实 V02 输入、DPI/多屏、受限账户、网络、长期资源趋势或物理 Windows 7 设备。
