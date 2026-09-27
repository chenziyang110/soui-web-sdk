# 当前 V02 Win7 SP1 x86 v4 交互验收被外部负载中断

修正后的 x86 Release 探针在外部 `probe-v4-build-attempt1` 真正编译成功，生成的工程与实际编译命令均使用静态 MSVC 运行库。`layout-interaction-v4-attempt1` 的 13 文件输入已冻结并通过只读校验，冻结 SHA-256 为 `4532ea7aa7bce0858d391d06339e8779dc9ad3aab7660f5c7e44a1b58077b4af`。它绑定当前 Demo 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 和 x86 Release buildId `f0c38056-058a-456e-be99-17446127f7ec`。

本次我方 QEMU PID 79452 于 2026-09-27 23:07:48 UTC 启动。截图确认真实 Win7 桌面后，于 23:09:06 UTC 只发送一次冻结命令。后续截图显示命令行窗口；串口仍为 0 字节。外部 QEMU PID 54876 于 23:09:56 UTC 启动，宿主门禁于 23:10:03 UTC 只停止我方客体。驱动原始退出 1、我方 QEMU 退出 -1，基础盘前后 SHA-256 不变、冻结复核退出 0。没有客体导出，因此不能判断应用是否执行、页面能否响应或中文 IME/缩放/任务并行结果。

`raw/` 保留客体原始日志、冻结检查、两次 PPM/PNG 截图、一次命令记录和空串口。`Archive-and-Review.cjs` 对复制原件逐字节校验，并可在暂存后用 `--check-index` 校验 Git blob。两份可能包含客体磁盘片段的 `vl.*` 快照只保留在外部原目录，不进入公开仓库。新的客体尝试必须使用新编号，先确认桌面后只发一次命令；该输入的 29 个动作和 96 帧仍属探索性，不能作为完整生产 IME 门槛。`win7CurrentV02Verified=false`，`releaseReady=false`。
