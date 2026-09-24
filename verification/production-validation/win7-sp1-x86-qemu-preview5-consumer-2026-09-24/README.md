# preview.5 Windows 7 SP1 x86 QEMU 公共消费者短验收

日期：2026-09-24。用无 Git 完整源码解压目录重新构建 `0.3.0-preview.5` x86 Release SDK，再安装公共头文件和库，从独立 `tests/acceptance-app` 编译真实 C++/Web 消费者。源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`，SDK 构建 ID `3304ca4f-17cf-4be7-a7bb-f91ca254f0a0`，消费者构建 ID `8cac9ce9-d3a3-484e-8ab1-3b7f71301a0b`。身份见 [sdk-build.json](sdk-build.json)、[consumer-build.json](consumer-build.json)。同一载荷在宿主 Windows 11 的四种模式各 3 轮均通过，四份监督结果保存在 [host-short-runs](host-short-runs)。

来宾为已安装的 Windows 7 Enterprise SP1 6.1.7601/32-bit；QEMU 11.1.0、TCG、Core 2 Duo、2 vCPU、2 GiB、标准 VGA、无网卡。虚拟机以快照模式启动，76 个运行载荷文件打包为 ZIP，经只读 vvfat D: 导入；来宾用 x86 7-Zip 验证并解压到 C: NTFS。四模式报告也写在 C: NTFS，通过 COM1 导出。ZIP、验收 EXE、`soui.dll`、`node.dll` 的来宾 SHA-256 与宿主输入一致，环境与散列见 [environment.json](environment.json)。

| 模式 | 完成轮数 | 退出码 | 完整步骤数 | 来宾用时 |
|---|---:|---:|---:|---:|
| Bridge | 3 | 0 | 10 | 39,004 ms |
| 重载 | 3 | 0 | 9 | 11,544 ms |
| 销毁重建 | 3 | 0 | 10 | 15,974 ms |
| 三窗口 | 3 | 0 | 34 | 53,711 ms |

[原始串口流](serial-export.txt)含 26 个文件的长度、SHA-256 和 Base64；[宿主校验脚本](Verify-SerialExport.ps1)复核完整帧、来宾系统身份、ZIP 操作、二进制散列、四个退出码、三轮采样和无失败事件，并调用[步骤校验](Verify-Events.cjs)检查所有步骤及资源样本顺序。还原的[逐模式事件和日志](results)可直接审阅。[来宾运行脚本](harness/run-preview5-consumer.cmd)与[串口导出脚本](harness/export-serial.ps1)保留；[完成画面](completed-screen.png)仅证明自动运行结束。复核命令：`powershell -NoProfile -ExecutionPolicy Bypass -File Verify-SerialExport.ps1`。来宾经 ACPI 正常关机，QEMU 退出 0。

这关闭 preview.5 在 Win7 SP1 x86 QEMU 的公共 SDK 软件路径四模式**三轮操作短验收**。QEMU TCG 用时不作为性能对照；三轮不能推断内存/句柄收敛。任务表满载业务、物理 Win7 x86、1,000 次循环、100 次重载资源门、8 小时混合负载、GPU、中文 IME/DPI、多屏、TLS 信任根/代理、安装升级及授权仍需独立验证。`releaseReady=false`。
