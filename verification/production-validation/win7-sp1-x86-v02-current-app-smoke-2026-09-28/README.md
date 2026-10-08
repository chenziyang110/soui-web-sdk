# 当前 V02 Win7 SP1 x86 Workbench 短测

本目录保存绑定当前 718 文件 Demo 源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 和正式 x86 Release 构建 ID `f0c38056-058a-456e-be99-17446127f7ec` 的**一次客体短测**。11 文件只读输入冻结 SHA-256 `6b11fc16ae74e00e528d14ab7aa301b49034348b8f8446a192c32b28c57445a3`；78 份原始文件见 `raw-manifest.json`，不依赖旧 717 文件应用证据。完整 ZIP 保存在外部 `C:\SOUI-SDK-production-validation\preview15-v02-win7-current-20260927\app-smoke-attempt1`，按冻结输入散列验证；仓库仅保存控制输入和载荷清单。

宿主确认 800×600 Win7 桌面后只发送一次冻结命令。客体身份为 6.1.7601/SP1/32 位/PowerShell 2，用 `--webview-smoke-test --arch=x86 --renderer=cpu --react --page=data --width=700 --height=500` 启动 Workbench。原生退出 0、原 60 秒内完成，日志按序记录 `application-ready`、Bridge 就绪、adapter 卸载、host 完整关闭及窗口销毁。Starter/Workbench 完整载荷分别 71/475 文件、各 5 份法律声明，解压后及运行后逐文件散列均一致；ZIP 测试/解压退出 0。

串口 29 帧、176,726 字节，SHA-256 `36f09ae09bf678b3baf9b36ee0454f1f5cc9c04bb924f8686265228e112dfcd0`，帧集合、长度、SHA-256 与客体导出均严格复核。62 次宿主采样无其他桌面负载，QEMU/驱动退出 0，基础盘与冻结输入前后不变。原始 PPM/PNG、命令发送、串口、客体日志与前后门禁保留。脚本参数和 React 页面导航日志不能证明 V02 页面在屏幕可见；本次没有执行真实滚轮、中文输入法、缩放或 30 秒任务并行。`win7CurrentV02Verified=false`、`releaseReady=false`。

客体结束后，第一次源码清单生成被新出现的外部 QEMU 门禁中止，原始退出 1、门禁、输出和被中断的进程身份也归档。此中断发生在客体短测结束之后，不能算源码清单通过，也不改变前述客体原始结果。
