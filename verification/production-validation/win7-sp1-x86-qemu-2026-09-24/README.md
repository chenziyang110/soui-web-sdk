# Windows 7 SP1 x86：preview.4 目标系统短验收

2026-09-24 在独立 QEMU 虚拟机中安装了 Windows 7 Enterprise SP1 32-bit。来宾 `wmic os` 确认版本 `6.1.7601`、`ServicePackMajorVersion=1`、`OSArchitecture=32-bit`。测试对象是固定源码指纹 `f4cb6eba6ff8ed6119d702b81df62c2db8279829afeeb8e035bd28e425cf22c9` 的 x86 Release 公共 SDK 独立消费者，而非仅在 Windows 11 上运行的 x86 进程。

QEMU 11.1.0 使用 `pc`/TCG、Core 2 Duo、2 vCPU、2 GiB 内存、标准 VGA，来宾没有网络接口。浏览器内核、SDK DLL 与验收程序的 SHA-256 记录在 [environment.json](environment.json)；ISO 仅记录散列，不随仓库分发。程序从 E: 载入，报告写入来宾 C: NTFS，再通过 COM1 串口导出。原始 [串口流](serial-export.txt)包含 17 个文件的长度、SHA-256 和 Base64 数据；[校验脚本](Verify-SerialExport.ps1)已逐项核验并还原到 [results](results)。

| 模式 | 完成轮数 | 退出码 | 最后事件 | 用时（QEMU TCG） |
|---|---:|---:|---|---:|
| Bridge | 3 | 0 | `run.complete` | 27,955 ms |
| 重载 | 3 | 0 | `run.complete` | 9,052 ms |
| 销毁重建 | 3 | 0 | `run.complete` | 11,292 ms |
| 三窗口 | 3 | 0 | `run.complete` | 48,434 ms |

每个事件流均有 3 个完成轮次采样，且没有失败事件。截图保留了[桌面](screenshots/desktop.png)、[SDK 页面](screenshots/sdk-page.png)、[多窗口](screenshots/multiwindow.png)与[串口导出](screenshots/serial-export.png)。这是 **Win7 SP1 x86 软件路径启动和短生命周期验收通过**，不是性能、资源稳定性或生产放行。TCG 时间不能与实际设备或 Electron 结果比较；仅 3 轮也不能证明内存/句柄收敛。

首次将虚拟磁盘放在当时空余约 3 GiB 的 F: 时，Windows 安装在组件配置阶段失败；安装日志屏幕显示 `0x8007045d / ERROR_IO_DEVICE`，见[错误弹窗](screenshots/setup-first-attempt-error.png)和[日志屏幕](screenshots/setup-first-attempt-log.png)。保留失败镜像后，改用空余约 87 GiB 的 D: 新建磁盘，同一 ISO 安装成功。不能仅凭这两次尝试断定首次失败的唯一原因。

最初把事件报告直接写到 QEMU 共享 FAT 盘时，重载与多窗口的 NDJSON 文件在来宾内就已截断，虽然程序报告退出码 0。因此本次有效结论只依据来宾 NTFS 上重新运行并通过串口核验的文件；[来宾执行脚本](harness/run-ntfs.cmd)和[串口导出脚本](harness/export-serial.ps1)已保留。76 个运行载荷文件从冻结构建复制；运行后仅 `resources/source-navigation.log` 因记录导航而改变，验收 EXE、SDK DLL、浏览器 DLL 和其余文件与源运行载荷一致。

尚未验证物理 Win7 x86 设备、中文 IME、DPI/多屏、TLS/代理/信任根、受限账户、安装签名与升级回退，也未完成业务 Demo V01–V06 在 Win7 上的完整链路或长时混合负载。候选版本仍是 preview，`releaseReady=false`。

复核原始结果：`powershell -NoProfile -ExecutionPolicy Bypass -File Verify-SerialExport.ps1`。脚本要求 17 个文件的帧、长度与 SHA-256 全匹配，并验证四种模式的退出码、三轮采样及最终 `run.complete`。
