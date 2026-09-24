# preview.6 Windows 7 SP1 x86 QEMU 应用短测准备

状态：**输入和校验器已冻结，客体尚未运行**；`releaseReady=false`。测试对象为 preview.6 x86 Release 的 Starter 与 Workbench，构建源码指纹和 build-id 在 [input.json](input.json)。[准备脚本](Prepare-Input.cjs)先用构建载荷清单逐文件核对，再压缩、测试 ZIP、重新解压并校验完整载荷。活动传输目录固定为 `D:\SOUI-SDK-QEMU-validation\guest-preview6-app-transfer-20260925-v3`。更早未启动的无后缀、`-v2` 临时目录已废弃，不得作为本次输入。v3 来宾脚本使用 CRLF，与仓库的 Windows 检出规则相符。

Starter ZIP 含 71 个运行文件，SHA-256 `D9730FC5D2B3D9B3276EA604A3B5CB92E01E151EB63F951E6539FA0956E3DC98`；Workbench ZIP 含 475 个，SHA-256 `3358F44A8C0C4762F54AFBF54B89159C49E6B629B42BBB316DC2B2F433AAF6A3`。两个载荷各含 `resources/legal/` 下的一份声明和四份原版许可文本；客体[校验脚本](harness/verify-apps.ps1)按 SHA-256 检查两套文件、数量及中文夹具路径。该脚本已在宿主从实际 ZIP 解压出的目录运行，输出 `Legal files=5x2,hashes=matched`。五份 PowerShell 脚本语法解析为 0 错误；[串口校验器](Verify-AppSerial.ps1)的 24 文件合成成功帧通过，篡改内核哈希的完整帧被拒绝。启动器在宿主八小时测试无终态时已验证拒绝启动，没有创建 QEMU 进程或启动记录。

宿主八小时测试形成终态且全部 x86/x64 QEMU 退出后，运行[启动器](harness/launch-preview6-apps.ps1)。它先校验已提交的输入记录、来宾脚本、两个 ZIP 和 x86 7-Zip 的散列，再以 `-snapshot`、只读 FAT 传输盘和无网卡启动已授权 Win7 SP1 x86 客体。先确认 VNC 显示 Win7 桌面就绪，再运行[一次性命令发送器](harness/run-preview6-apps-keys.ps1)。来宾在 C: NTFS 解包并执行 Starter、Workbench 默认页和 React 页短 smoke，结果经 COM1 导出；最后运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Verify-AppSerial.ps1` 核对帧、文件散列、Win7 SP1 x86 身份、输入 ZIP/二进制身份、许可文件检查日志、三次退出码及就绪/关闭终点。失败时保留原始样本，不覆盖已冻结输入。

本场景只验证当前候选的运行包、许可材料文件身份和三个短启动/关闭路径；不证明许可文本已完整覆盖实际链接成员，也不替代真实 V02 交互、中文 IME、DPI/多屏、网络、物理设备、长期稳定性或产品发布验收。此来宾必须与其他 Win7 探针串行执行。
