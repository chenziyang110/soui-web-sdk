# Win7 SP1 x86 目录对话框取消短测 v2

[v1 冻结候选](../win7-sp1-x86-qemu-directory-2026-09-25/README.md)的原始 `soui_directory_dialog_tests.exe` SHA-256 为 `6E7D6BE3E88E02CBDAC203A5BC7E425746413ECFA6A4E489F827B7B3FDCBB7F6`。该程序动态依赖 VC 运行库；同系列[桌面服务 v1](../win7-sp1-x86-qemu-desktop-2026-09-25/README.md)曾因缺少 `MSVCP140.dll` 在 Win7 加载前失败。v2 保持本测试 EXE 字节不变，从已经在客体运行的[桌面服务 v2](../win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)冻结 52 个应用本地 VC/UCRT DLL。[输入记录](input.json)逐个列出字节数和 SHA-256；[准备脚本](Prepare-Input.cjs)核对后生成独立只读传输目录，不修改 v1 样本。

测试程序在实际窗口与 STA 工作线程上打开 Shell 目录对话框，观察到对话框后发出取消，并要求公共 Bridge 返回取消状态。宿主无其他 QEMU 时先运行[启动器](harness/launch-directory.ps1)，确认 Win7 桌面后只运行一次[命令发送器](harness/run-directory-keys.ps1)，最终用[串口校验器](Verify-SerialExport.ps1)核对系统、EXE、退出码、输出和逐文件哈希。使用 `-snapshot`、只读 FAT 传输盘和无网卡；`releaseReady=false`。

当前状态：独立传输已准备，56 个文件及脚本散列已冻结；**客体尚未执行**。即使取消短测通过，也未覆盖模板点击、正向中文路径、受限账户、网络路径、关窗/导航竞态或生产门槛。
