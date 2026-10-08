# 当前 V02 Win7 SP1 x86 v4 交互探索原始记录

客体使用先前冻结且只读验证的 13 文件输入，freeze SHA-256 `4532ea7aa7bce0858d391d06339e8779dc9ad3aab7660f5c7e44a1b58077b4af`，绑定当前 Demo 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 和正式 x86 Release buildId `f0c38056-058a-456e-be99-17446127f7ec`。同一输入的首次尝试在命令执行期间受到另一台 QEMU 干扰，原件另存于[首次中断记录](../win7-sp1-x86-v02-interaction-v4-interrupted-2026-09-28/README.md)。

本次我方 QEMU PID 5708 于 2026-09-27 23:26:21 UTC 启动。两张截图确认 Win7 桌面就绪，随后只发送一次冻结命令。严格的 `Review-LayoutInteractionV4.cjs` 原始退出 0：串口 215389 字节、96 帧，客体报告 Windows 6.1.7601 SP1、32 位、PowerShell 2，资源与五份法律文件前后身份一致。当前 V02 数据页已出现并加载万行，原生输入探针记录 29 次动作，窗口缩放前后边界变化符合预期；应用原生退出 0，正常关闭，QEMU 自身退出 0，基础盘及冻结输入前后不变。这些是对应冻结输入的功能观察，不是完整中文 IME 或任务并行验收。

宿主每五秒采样的 62 个门禁记录均为空，但外部 QEMU PID 60968 在最后一次采样约 70 毫秒后、我方 QEMU 退出前启动。串口文件最后写入早于该外部进程启动；本目录保留随后的真实 CIM 快照。故不能声称整段客体运行完全隔离，更不能据此得出资源或时延结论。`Archive-and-Review.cjs` 对复制原件、外部原件和暂存 Git blob 逐字节核验，并明确保持 `fullHostIsolation=false`。

`raw/` 包含原始串口、客体身份/动作校验结果、过程门禁、截图、退出记录与外部进程观察。QEMU 正常退出后没有遗留 `vl.*` 临时快照。当前 `taskStartedVerified=false`、`imeCompositionVerified=false`、`taskParallelVerified=false`、`win7CurrentV02Verified=false`、`releaseReady=false`；真实中文 IME 组合、公共任务进度与页面并发响应仍需另行验收。
