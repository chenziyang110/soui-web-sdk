# preview.5 Windows 7 SP1 x86 QEMU 应用短验收

日期：2026-09-24。候选 `0.3.0-preview.5`，源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`，x86 Release 构建 ID `426d9be7-c49a-43df-a4c1-7532087a23e2`。来宾为已安装的 Windows 7 Enterprise SP1 6.1.7601/32-bit；QEMU 11.1.0、TCG、Core 2 Duo、2 vCPU、2 GiB、标准 VGA、无网卡。本次用快照模式启动，载荷经只读 vvfat D: 导入，运行结果保存在来宾 C: NTFS 并经 COM1 导出。详情见 [environment.json](environment.json)。

当前构建的模板 68 文件和 Workbench 473 文件分别封装为 ZIP；来宾用独立 x86 7-Zip 验证、解压，并核对 ZIP、应用 EXE、`soui.dll`、`node.dll` 的 SHA-256 及两个中文文件名。模板 `--smoke-test`、Workbench 默认页及 `--react` 的 `--webview-smoke-test` 均退出 0；模板日志以 `host-close-complete` 结束，两个 Workbench 日志记录文档/Bridge/脚本探针就绪并以 `window-destroyed` 结束。[完成画面](completed-screen.png)只显示自动化脚本结束，不作为视觉质量验收。

[原始串口流](serial-export.txt)含 24 个文件及各自长度、SHA-256、Base64；[宿主校验脚本](Verify-AppSerial.ps1)复核帧、文件集、系统身份、关键载荷散列、退出码和日志终点，还原文件在 [results](results)。复核命令：`powershell -NoProfile -ExecutionPolicy Bypass -File Verify-AppSerial.ps1`。来宾[运行脚本](harness/run-preview5.cmd)、[文件/中文路径校验](harness/verify-apps.ps1)及[串口导出脚本](harness/export-serial.ps1)一并归档。来宾通过 ACPI 正常关机，QEMU 进程退出 0。

这关闭 preview.5 在 Win7 SP1 x86 QEMU 的三项应用短启动/关闭切片；同版[四模式公共消费者三轮短测](../win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)另行归档。本次未执行任务容量满载业务、物理设备、资源趋势、中文 IME/DPI、TLS/代理、完整 Demo V01–V06、安装升级、签名、8 小时混合负载或 Electron 等负载对照。`releaseReady=false`。
