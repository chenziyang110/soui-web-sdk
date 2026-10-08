# v3 Win7 SP1 x86 原始失败样本

2026-09-25 已授权 Windows 7 Enterprise SP1 x86 QEMU 来宾从冻结 v3 只读传输盘启动。输入身份见上级 [input.json](../input.json)。桌面截图、启动和按键发送记录、完整 COM1 帧及 14 个解码文件均在本目录。`Verify-AppSerial.ps1` 报 `Expected 24 files; got 14`，`verify-apps-exitcode.txt` 为 1。

应用 ZIP 已成功测试及解压；`verify-apps.log` 记录两个文件数和中文路径检查后，在许可文件散列检查处报告来宾 PowerShell 2 无 `SHA256Managed.Dispose` 方法。批处理按失败分支导出结果，没有运行 Starter、Workbench 默认页或 React 页。此为测试夹具兼容性失败，不能判定应用通过或失败。后续修复以新输入单独重测，不覆盖本目录；`releaseReady=false`。
