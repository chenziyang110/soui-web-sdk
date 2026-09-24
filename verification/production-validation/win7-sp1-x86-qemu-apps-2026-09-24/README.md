# Windows 7 SP1 x86：模板与 Workbench 短验收

日期：2026-09-24。候选：`0.3.0-preview.4`，源码指纹 `f4cb6eba6ff8ed6119d702b81df62c2db8279829afeeb8e035bd28e425cf22c9`。与[同一虚拟机的公共 SDK 消费者四模式短测](../win7-sp1-x86-qemu-2026-09-24/README.md)配套，本次进一步运行了 x86 Release 模板 `SouiStarter` 和 Demo `SouiWorkbench`。系统为独立安装的 Windows 7 Enterprise SP1 6.1.7601、32-bit；QEMU 11.1.0/TCG、Core 2 Duo、2 vCPU、2 GiB、标准 VGA、无网卡。完整身份与散列见 [environment.json](environment.json)。ISO 与虚拟磁盘不进入仓库。

从同一冻结构建取出应用载荷，模板 68 个文件、Workbench 473 个文件。因 QEMU vvfat 对两个中文文件名报编码警告，载荷先按原名封装为 ZIP，由 [7-Zip 官方 19.00 x86 Extra](https://www.7-zip.org/download.html)中的独立 `7za.exe` 在来宾校验并解压到 C: NTFS。两个 ZIP 的来宾 SHA-256 与宿主一致；来宾再次核对应用、`soui.dll`、`node.dll` 的 SHA-256。解压文件数分别为 68/473，两个中文夹具文件名存在。7-Zip 工具只用于本次测试，不随 SDK 交付。

| 场景 | 退出码 | 日志终点 | 结果 |
|---|---:|---|---|
| 模板 `--smoke-test` | 0 | `host-close-complete` | 启动、就绪、关闭通过 |
| Workbench 默认页 `--webview-smoke-test` | 0 | `window-destroyed` | 文档、Bridge、脚本探针就绪并关闭 |
| Workbench `--react --webview-smoke-test` | 0 | `window-destroyed` | 同一探针与关闭路径通过 |

来宾结果写在 C: NTFS 并经 COM1 导出。[原始串口流](serial-export.txt)含 24 个文件的长度、SHA-256 与 Base64；[校验脚本](Verify-AppSerial.ps1)复核帧、文件集、系统身份、压缩包及二进制散列、退出码与日志终点，恢复结果到 [results](results)。[运行脚本](harness/run-apps.cmd)、[夹具校验](harness/verify-apps.ps1)和[串口导出脚本](harness/export-serial.ps1)保留供复核。命令：`powershell -NoProfile -ExecutionPolicy Bypass -File Verify-AppSerial.ps1`。

另以正常交互模式打开 Workbench，留存[默认页截图](screenshots/workbench-default.png)与[关闭默认进程后以 `--react` 启动的截图](screenshots/workbench-react.png)。两种模式均渲染工具首页。烟测的通用脚本探针不单独输出 React DOM 身份，因此该结果只证明按 `--react` 启动并完成现有短探针；React 的完整业务功能仍需按场景验收。

前两次自动化尝试未进入应用：第一次是来宾脚本路径参数解析错误，第二次是 Windows 7 Shell ZIP 接口无法打开归档；均不计为 SDK 通过或失败。使用独立 x86 解压器后才生成这里的有效结果。测试使用软件渲染且仅为短时运行；QEMU TCG 的用时不能与物理机或 Electron 对比。物理 Win7 x86、完整 Demo V01–V06、多窗口业务链、中文 IME、DPI、TLS/代理、8 小时混合负载、资源收敛及安装/升级/授权尚未验收。`releaseReady=false`。
