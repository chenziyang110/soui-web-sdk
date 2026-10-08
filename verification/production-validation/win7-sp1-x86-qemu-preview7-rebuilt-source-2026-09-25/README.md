# preview.7 重编依赖在 Windows 7 SP1 x86 QEMU 的短测

状态：**三项应用入口短测通过**；`releaseReady=false`。本次载荷为 preview.7 x86 Release 模板与 Workbench，运行目录中的 Miniblink `node.dll` 和五个 SOUI DLL 改用[仓库外源码重建产物](../preview6-relocated-dependency-encoding-2026-09-25/README.md)。这是一份明确标记的诊断组合，不等同于已发布的固定 SDK 包，也不把旧 preview.6 证据改写成 preview.7 结果。

[冻结输入](input.json)记录应用构建 ID、源码指纹、两份 ZIP 与完整载荷散列，重建内核 SHA-256 为 `FFAAC5EEF1E520BAD5C5E450BF0A380B32871A4701ED452D4E28E7EA726FB68B`。宿主先对 ZIP 执行完整性测试、重新解包并逐文件比对；[来宾校验脚本](harness/verify-apps.ps1)确认 72/477 个文件、中文夹具路径和两应用各五份声明文件的散列。

[QEMU 启动记录](evidence/launch.json)使用既有 Windows 7 基础镜像、`-snapshot`、只读 FAT 传输盘和无网卡；[桌面画面](evidence/boot-desktop.png)确认进入来宾桌面后只[发送一次命令](evidence/key-send.json)。来宾在 NTFS 中解包并执行 Starter、Workbench 默认页和 React 页；[终态画面](evidence/complete-console.png)显示三项均退出 0，并经 COM1 导出 24 份结果。虚拟机已退出，基础镜像未写回。

[原始串口帧](serial-export.txt)由[核验器](Verify-AppSerial.ps1)逐文件检查长度和 SHA-256，再核对来宾 `6.1.7601`、SP1、32-bit 身份，ZIP/EXE/内核/SOUI 散列、全部测试与解包退出码，以及三份应用日志中的就绪和关闭事件。[核验输出](evidence/verifier-output.txt)为 24/24；[反例自检](evidence/self-test.json)确认改错内核散列会被拒绝。[Starter](results/starter-smoke.log)、[Workbench 默认页](results/workbench-default-smoke.log)和[React 页](results/workbench-react-smoke.log)原始日志均保留。

此项只证明指定组合在 Win7 SP1 x86 QEMU 的启动、桥接就绪与正常关闭；不覆盖真实输入、多窗口、DPI/IME、受限账户、网络/信任根、长时资源、物理设备或最终源码包的完整可重建性。底层内核安全维护和再分发授权仍未完成，不能据此放行生产。
