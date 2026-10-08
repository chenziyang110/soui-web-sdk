# Win7 SP1 x86 公共剪贴板写入准备

状态：**输入及脚本已准备，来宾尚未执行**；`releaseReady=false`。此项只验证公共 C++ 服务在目标系统实际提交并读回中文 `CF_UNICODETEXT`，不等于模板 Web 按钮或全部 V03 桌面能力通过。

源代码候选为提交 `231de4cc`，SDK 源码清单、x86 静态库与测试 EXE 的 SHA-256 固定在 [input.json](input.json)。来宾传输在 `D:\SOUI-SDK-QEMU-validation\guest-clipboard-transfer-20260925`，包含测试 EXE、输入记录和[命令脚本](harness/run-clipboard.cmd)。传输文件与冻结记录已逐项核对；四份 PowerShell 脚本通过语法解析，串口校验器通过一份合成的完整 Win7 x86 帧/哈希/退出码自检。测试用同进程真实窗口作为剪贴板所有者，将中文写入系统剪贴板，再读取并逐字比较。

宿主八小时测试得到终态、且所有 x86/x64 QEMU 退出后，先运行[启动器](harness/launch-clipboard.ps1)。其只读 FAT 传输盘、`-snapshot` 和无网卡使客体测试不写回基础镜像。启动器只启动虚拟机；确认 VNC 或监控截图是 Win7 桌面后，运行[一次性命令发送器](harness/run-clipboard-keys.ps1)。来宾经 COM1 导出操作系统身份、EXE 哈希、退出码和原始输出；用[串口校验器](Verify-SerialExport.ps1)核对完整帧、每个文件的 SHA-256、Win7 SP1 x86 身份、EXE 身份及实际测试结果。失败时保留原始串口和日志，以新输入记录复测，不覆盖原样本。

该项必须与[公共任务客体](../win7-sp1-x86-qemu-public-task-2026-09-25/README.md)及 preview.5 旧版混合客体串行执行。即使此短测通过，真实网页点击、剪贴板被占用、长文本、受限账户和物理 Win7 设备仍待验证。
