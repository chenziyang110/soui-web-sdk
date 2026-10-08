# 当前 V02 Win7 SP1 x86 v6 客体中断原始记录

冻结的 13 文件输入 SHA-256 为 `8e9b6d96266e3f72af6e49c498efd942886266bd9fbfdbc71d6e9f58df5b830f`，绑定当前 Demo 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`、正式 x86 Release buildId `f0c38056-058a-456e-be99-17446127f7ec`。v5 输入曾因首个滚轮坐标超出初始窗口宽度而在执行前被拒绝，保留 `raw/v5-not-executed-static-refusal.json`；v6 修正坐标并重新冻结，没有改写 v5。

我方 QEMU PID 75276 于 2026-09-28 00:08:04 UTC 启动。宿主两次采样无竞争负载，之后外部 QEMU PID 63876 启动；第三次采样检测到它，仅终止我方客体。驱动原始退出 1、我方 QEMU 退出 -1，串口为 0 字节且没有 `command-send.json`。客体桌面未确认、冻结命令未发送，因而这次既不证明应用失败，也不证明通过。基础盘前后 SHA-256 一致，冻结输入前后核验均通过。

`raw/` 保存 17 份复制原件及逐文件 SHA-256 清单，包含驱动原始输出、启动与过程门禁、冻结核验、QEMU 结果以及控制脚本预检。两份 `vl.*` 客体临时磁盘快照仅保留在外部原件目录，未进入仓库；清单记录它们的名称、大小和散列。`Archive-and-Review.cjs` 复核外部原件、仓库副本及暂存 Git blob，明确 `commandSent=false`、`win7CurrentV02Verified=false`、`releaseReady=false`。

下一次只在宿主无其他桌面/编译/客体负载时使用同一只读冻结输入的新客体 attempt；先确认 Win7 桌面，再仅发送一次命令，保留失败原样本。真实中文 IME 组合、30 秒公共任务与页面操作并行及隔离性能结论仍未验收。
