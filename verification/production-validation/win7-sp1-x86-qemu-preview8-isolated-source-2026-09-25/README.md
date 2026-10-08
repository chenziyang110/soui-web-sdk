# preview.8 源码重编载荷在 Windows 7 SP1 x86 QEMU 的短测

状态：**三个应用入口短测通过；生产准入未通过**。[输入清单](input.json)绑定 preview.8 冻结候选源码 ZIP、x86 Release 构建 ID/源码指纹、两个应用 ZIP/EXE、重编 `node.dll` 与 `soui.dll` 散列。载荷来自[同一无 Git 完整源码目录](../preview8-isolated-source-build-2026-09-25/README.md)；x86 Miniblink 与五个 SOUI 组件均从随包源码重编。两份应用载荷分别为 Starter 71 文件、Workbench 475 文件；宿主对 ZIP 测试、解包并逐文件核对[清单](SouiWorkbench-payload-manifest.json)。

[QEMU 启动记录](evidence/launch.json)使用既有 Windows 7 SP1 x86 镜像、`-snapshot`、只读 FAT 传输盘、无网卡。来宾在 NTFS 解包，校验文件数量、中文夹具路径和声明材料散列，再运行 Starter、Workbench 默认页及 React 页；[完成画面](evidence/complete-console.png)显示三项退出码 0 和 24 份结果已导出。QEMU 已退出，基础镜像未写回。

[串口原始帧](serial-export.txt)导出 24 份客体结果。[宿主核验器](Verify-AppSerial.ps1)逐项检查长度与 SHA-256，核对系统 `6.1.7601`/SP1/32-bit 身份、ZIP/EXE/引擎/SOUI 散列、解包退出码、应用就绪与正常关闭日志；[核验输出](verifier-output.txt)为 24/24。[反例自检](self-test-output.txt)确认篡改引擎散列会被拒绝。客体原始文件保存在 [results](results/)；传输与客体运行脚本保存在 [harness](harness/)。

这是虚拟机上的功能短测，不是物理 Win7 x86、完整业务、长时资源、性能、系统信任根/代理、IME/DPI 或签名安装验收。运行期间另有主机构建任务，时间和资源数据不用于性能结论；`releaseReady=false`。
