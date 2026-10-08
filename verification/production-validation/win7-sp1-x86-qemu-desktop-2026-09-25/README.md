# Win7 SP1 x86 公共桌面服务短测准备

状态：**已执行来宾，但测试 EXE 因缺少 `MSVCP140.dll` 未进入功能测试**；`releaseReady=false`。这组测试原拟在目标客体创建真实 HWND，验证公共 C++ 服务的中文标题、最小化、最大化/还原、关闭及剪贴板中文写入读回。它不运行模板 WebView，也不代表 V03 整组、物理设备或生产准入通过。

源代码候选为提交 `4cd4f34b`；源码清单、x86 SDK 静态库和测试 EXE 的 SHA-256 固定在 [input.json](input.json)。只读来宾传输位于 `D:\SOUI-SDK-QEMU-validation\guest-desktop-transfer-20260925`，包含测试 EXE、输入记录和[命令脚本](harness/run-desktop.cmd)。传输与输入记录逐项相符，四份 PowerShell 脚本通过语法解析，串口校验器通过合成的完整 Win7 x86 帧/散列/退出码自检。该脚本调用 `--desktop-roundtrip`；与更早的[纯剪贴板候选](../win7-sp1-x86-qemu-clipboard-2026-09-25/README.md)分别保留身份和结果。

宿主八小时测试形成终态且所有 x86/x64 QEMU 退出后，先运行[启动器](harness/launch-desktop.ps1)。它使用 `-snapshot`、只读 FAT 传输盘和无网卡，不写回基础镜像。确认 VNC 或监控截图是 Win7 桌面后，再运行[一次性命令发送器](harness/run-desktop-keys.ps1)。客体经 COM1 导出系统身份、EXE 哈希、退出码和原始日志；用[串口校验器](Verify-SerialExport.ps1)核对文件帧/散列、Win7 SP1 x86、冻结 EXE 和测试结果。失败时保存原始数据并用新输入记录复测。

2026-09-25 实际来宾已启动，桌面确认后只发送一次测试命令。[系统错误画面](evidence/missing-msvcp140.png)明确显示缺少 `MSVCP140.dll`；[COM1 原始帧](serial-export.txt)回传 5 个文件、测试退出码 `-1073741515`（`0xC0000135`），[主机校验器](Verify-SerialExport.ps1)判失败。原始启动、按键、截图、解码文件和失败原因均已保留。**没有执行桌面功能调用**；此为独立测试 EXE 传输未带应用本地 VC/UCRT 运行库的问题，不能判定 SDK 功能通过或失败。保持本次输入不变，另行准备[含锁定运行库的 v2 输入](../win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)重测。

即使重测通过，本短测仍缺少真实页面按钮操作、跨线程阻塞与关闭竞态、多窗口、受限账户、剪贴板占用、DPI/IME 和物理 Win7 设备验证。客体运行必须与其他探针串行。
