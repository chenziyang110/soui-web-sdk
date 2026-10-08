# Win7 V02 V8 客体：键盘输入后遭外部负载中断

此轮沿用 [已冻结的 V8 输入](../win7-sp1-x86-v02-v8-prelaunch-refusal-2026-09-28/README.md)，冻结文件 SHA-256 为 `764452636fd31dd5cac25611882f67fe578a7fe8357e36ce9d79a9826166502b`，绑定旧候选 718 文件源码和正式 x86 Release buildId `f0c38056-058a-456e-be99-17446127f7ec`。`guest-layout-interaction-v8-attempt2` 是独立的新尝试，没有覆盖启动前拒绝的 attempt1。

宿主空闲时启动我方 `qemu-system-i386.exe` PID 79304。启动前截图显示真实 Win7 桌面；宿主门禁再次确认没有外部桌面负载后，监视器向快照客体发送了一次冻结命令的 Win+R 和键盘序列。`command-send.json` 及监视器原始记录证明按键已发送，**不能证明客体实际执行了命令**。此后串口始终为 0 字节，未取得客体身份、原生退出码或应用交互结果。

外部 `qemu-system-x86_64.exe` PID 45436 于 2026-09-28 09:32:07Z 出现，五秒门禁在 09:32:10Z 检出。驱动只终止我方客体，原始驱动退出 1、我方 QEMU 退出 -1；基础盘 SHA-256 前后一致，冻结输入复核退出 0。此次不能判断 Workbench、任务、中文 IME、缩放是否通过或失败，也不能作为隔离资源/时延样本。`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。

[20 份精选原件及逐文件 SHA-256](raw-manifest.json)包含截图原始 PPM 与 PNG、门禁、冻结输入复核、监视器输入记录、串口空文件和双层退出码。两份可能含客体磁盘片段的大型 QEMU 临时快照留在外部原件目录，不进入公开仓库。`node Archive.cjs verify` 可复核仓库副本及存在时的外部原件；暂存后运行 `node Verify-Index.cjs` 对照 Git index。下轮必须使用新 guest attempt 名；优先验证窗口工作区修复的新构建，不能把本次旧候选 V8 输入当作修复验收。
