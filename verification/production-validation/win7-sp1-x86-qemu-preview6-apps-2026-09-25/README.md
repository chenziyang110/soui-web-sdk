# preview.6 Windows 7 SP1 x86 QEMU 应用短测准备

状态：**v3 客体已运行但验证失败，三次应用 smoke 均未开始**；`releaseReady=false`。测试对象为 preview.6 x86 Release 的 Starter 与 Workbench，构建源码指纹和 build-id 在 [input.json](input.json)。[准备脚本](Prepare-Input.cjs)先用构建载荷清单逐文件核对，再压缩、测试 ZIP、重新解压并校验完整载荷。v3 传输目录固定为 `D:\SOUI-SDK-QEMU-validation\guest-preview6-app-transfer-20260925-v3`。更早未启动的无后缀、`-v2` 临时目录已废弃。v3 来宾脚本使用 CRLF，与仓库的 Windows 检出规则相符。

Starter ZIP 含 71 个运行文件，SHA-256 `D9730FC5D2B3D9B3276EA604A3B5CB92E01E151EB63F951E6539FA0956E3DC98`；Workbench ZIP 含 475 个，SHA-256 `3358F44A8C0C4762F54AFBF54B89159C49E6B629B42BBB316DC2B2F433AAF6A3`。两个载荷各含 `resources/legal/` 下的一份声明和四份原版许可文本；客体[校验脚本](harness/verify-apps.ps1)按 SHA-256 检查两套文件、数量及中文夹具路径。该脚本已在宿主从实际 ZIP 解压出的目录运行，输出 `Legal files=5x2,hashes=matched`。五份 PowerShell 脚本语法解析为 0 错误；[串口校验器](Verify-AppSerial.ps1)的 24 文件合成成功帧通过，篡改内核哈希的完整帧被拒绝。启动器在宿主八小时测试无终态时已验证拒绝启动，没有创建 QEMU 进程或启动记录。

宿主八小时测试结束后，2026-09-25 07:38 启动 v3 来宾；[启动记录](failed-v3/launch.json)固定 QEMU PID 与输入，VNC [桌面截图](failed-v3/guest-screen-after-run.png)确认 Win7 已进入桌面，随后只发送一次命令。来宾测试 ZIP 并在 C: NTFS 解压两个应用，文件数量分别为 71 和 475，中文夹具路径也存在。但 `verify-apps.ps1` 在计算许可文件散列后调用 `SHA256Managed.Dispose()`，Win7 自带的旧版 PowerShell 不公开该方法，检查退出码为 1；批处理因此跳过全部三次应用 smoke，只经 COM1 [导出 14 个文件](failed-v3/serial-export.txt)。[主机串口校验器](Verify-AppSerial.ps1)正确拒绝缺少 10 个必需文件的帧，原始解码和失败原因保存在 [failed-v3](failed-v3/README.md)。v3 不能证明 preview.6 应用在 Win7 可运行；QEMU 已退出，快照写入未进入基础镜像。

只调整来宾 PowerShell 的散列对象清理调用、保留相同应用载荷的 [v4 独立输入](../win7-sp1-x86-qemu-preview6-apps-v4-2026-09-25/README.md)已另行准备。v3 的 ZIP、脚本、串口与失败记录保持原样，不把 v4 结果回填进本次样本。

本场景只验证当前候选的运行包、许可材料文件身份和三个短启动/关闭路径；不证明许可文本已完整覆盖实际链接成员，也不替代真实 V02 交互、中文 IME、DPI/多屏、网络、物理设备、长期稳定性或产品发布验收。此来宾必须与其他 Win7 探针串行执行。
