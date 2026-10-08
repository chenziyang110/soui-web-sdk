# Win7 SP1 x86 公共桌面服务短测 v2

[v1 原始失败样本](../win7-sp1-x86-qemu-desktop-2026-09-25/README.md)显示测试 EXE 缺少应用本地 `MSVCP140.dll`，退出码 `0xC0000135`，未进入桌面服务。v2 保持同一个 `soui_desktop_unicode_tests.exe` 的原始 SHA-256 `344F4B72F0BF05389C6983EE6728B6B9230035E3FE030174ADF90BE70C3D41EB`，从 preview.6 x86 Starter 的已验证运行载荷中补入 52 个 VC/UCRT DLL。逐文件字节数、SHA-256、源构建 ID、应用载荷指纹和原测试 EXE 身份在 [input.json](input.json) 中冻结；[准备脚本](Prepare-Input.cjs)先核对两份原始构建记录，再复制到新的只读传输目录 `D:\SOUI-SDK-QEMU-validation\guest-desktop-v2-transfer-20260925`。`releaseReady=false`。

2026-09-25 已在实际 QEMU Win7 Enterprise SP1 x86 客机执行。[启动器](harness/launch-desktop.ps1)逐文件核对 52 个运行库及原 EXE、脚本和输入记录，使用 `-snapshot`、只读 FAT 传输盘与无网卡启动客机。确认[桌面截图](evidence/boot.png)后只运行一次[按键发送器](harness/run-desktop-keys.ps1)；来宾以原 EXE 执行 `--desktop-roundtrip`，经 COM1 导出系统身份、EXE 散列、退出码和原始输出。[串口校验器](Verify-SerialExport.ps1)核对完整帧、5 份文件哈希、Win7 SP1 x86 身份、冻结 EXE 身份、退出码 0 和成功标记，生成[验证结果](results/verification.json)。[原始串口](serial-export.txt)、[原始输出](results/desktop-stdout.log)、[执行后截图](evidence/after.png)、QEMU 启动和按键记录均已归档；快照客机已退出。该测试程序实际执行公共剪贴板中文写入读回和公共窗口标题中文往返。

此短测只覆盖公共 C++ 服务的真实窗口和剪贴板路径。即使通过，也不代表模板网页按钮、关闭竞态、DPI/IME、多屏、受限账户、物理 Win7 设备或生产准入通过。52 个运行库在应用包中的存在只证明本次字节交付，不能替代其再分发许可审查。
