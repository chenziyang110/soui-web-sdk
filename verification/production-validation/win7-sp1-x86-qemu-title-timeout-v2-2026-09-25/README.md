# Win7 SP1 x86 窗口线程停滞短测 v2

[v1 冻结输入](../win7-sp1-x86-qemu-title-timeout-2026-09-25/README.md)的原始测试 EXE 仍按 SHA-256 `3E70607AC229B4078201181157BECBB36314884318C6A004F5FA777BC90E8626` 使用。鉴于同系列的[桌面服务 v1](../win7-sp1-x86-qemu-desktop-2026-09-25/README.md)实际在 Win7 因缺少 `MSVCP140.dll` 而未进入测试，v2 在新的只读传输目录中补齐[桌面服务 v2](../win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)已验证的 52 个应用本地 VC/UCRT DLL。每个 DLL 的字节数和 SHA-256、来源构建及原 EXE 身份固定于 [input.json](input.json)。[准备脚本](Prepare-Input.cjs)核对原始输入与运行库后生成独立传输目录，不修改 v1 样本。

本测试要在真实 Win7 SP1 x86 客机中让窗口所有者线程暂停约 1.2 秒，确认公共 `setTitle` 在 600 ms 内返回 `E_WINDOW_TIMEOUT`、失效的标题没有晚到写入，并在窗口线程恢复后成功写入中文标题。宿主无其他 QEMU 时先运行[启动器](harness/launch-title.ps1)，确认桌面后仅运行一次[命令发送器](harness/run-title-keys.ps1)，最后以[串口校验器](Verify-SerialExport.ps1)核对系统、程序、退出码和原始输出。测试使用 `-snapshot`、只读 FAT 传输盘和无网卡；`releaseReady=false`。

2026-09-25 已在实际 Win7 Enterprise SP1 x86 QEMU 客机执行：传输 56 个文件及脚本散列由启动器核对，确认[桌面截图](evidence/boot.png)后一次性发送命令。来宾退出码 0，[串口校验器](Verify-SerialExport.ps1)对完整帧、5 份文件哈希、系统/EXE 身份和成功标记均判定通过，生成[验证结果](results/verification.json)。[原始串口](serial-export.txt)、[原始输出](results/title-stdout.log)、[执行后截图](evidence/after.png)、QEMU 启动和按键记录均已归档；快照客机已退出。

此短测仍不能代替模板真实页面操作、关闭竞态、多窗口、物理 Win7 x86 或生产资源门槛。
