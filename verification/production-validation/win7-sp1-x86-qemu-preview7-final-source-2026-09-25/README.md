# preview.7 最终源码重编载荷在 Windows 7 SP1 x86 QEMU 的短测

状态：**三项应用入口短测通过；生产未通过**。载荷来自[原封 preview.7 完整源码包](../preview7-final-source-bottom-rebuild-2026-09-25/README.md)的全新 x86 解压目录，先从源码重编 Miniblink 和 SOUI，再编译 Release Starter/Workbench；不是先前诊断目录的 DLL，也不是固定预编译 SDK 包。源码包 SHA-256 为 `A4F0703A4C6F6E13DC4A07BC59174D347487622143AAB51E82B3D3F4190D802E`，重编 `node.dll` 为 `134412B631E10A98083D36B7CE42FE20386769A63F50EA8545CA3EF70C23907E`。

[冻结输入](input.json)记录构建 ID、源码指纹、两份 ZIP、EXE、引擎和 SOUI 散列；[Starter](SouiStarter-payload-manifest.json)与[Workbench](SouiWorkbench-payload-manifest.json)完整载荷清单分别为 71、475 个文件。宿主测试了两份 ZIP，解包后逐文件比对散列。来宾[校验脚本](harness/verify-apps.ps1)再次检查文件数、中文夹具路径和各五份声明文件散列。

[QEMU 启动记录](evidence/launch.json)使用既有 Windows 7 SP1 基础镜像、`-snapshot`、只读 FAT 传输盘、无网卡；[开机桌面](evidence/boot-desktop.png)和[完成画面](evidence/complete-console.png)记录实际可见状态。来宾从 NTFS 解包并执行 Starter、Workbench 默认页和 React 页，三项均退出 0。QEMU 已正常退出，基础镜像未写回。

[串口原始帧](serial-export.txt)导出 24 份客体结果。[核验器](Verify-AppSerial.ps1)逐项检查长度与 SHA-256，并核对 Windows `6.1.7601`、SP1、32-bit 身份、ZIP/EXE/内核/SOUI 散列、解包与运行退出码，以及应用就绪和正常关闭日志；[核验输出](verifier-output.txt)为 24/24。[反例自检](self-test-output.txt)确认篡改内核散列会被拒绝。客体原始结果见[results](results/)。

这只证明该版本的指定重编载荷在 Win7 SP1 x86 虚拟机中能短启动并正常关闭。虚拟机与本机其他编译/虚拟机任务曾并行，时间和资源指标不用于性能判断。新版公共消费者、真实页面操作、八小时资源、物理 Win7 x86、DPI/IME、签名安装、网络安全与授权仍未验收；`releaseReady=false`。
