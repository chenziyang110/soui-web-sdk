# 当前 V02 Win7 SP1 x86 交互尝试的隔离中断

本目录保存第一次 `guest-layout-interaction-v3-attempt1` 的 47 份复制原始记录、复制散列清单和只读复核程序。新 13 文件冻结输入 SHA-256 为 `4a8eb139db54d993821da4e95a63f9100e14c3d3d91fab7afbdc34a6dad5e8a3`，绑定 718 文件 Demo 源码 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 与正式 x86 Release 构建 `f0c38056-058a-456e-be99-17446127f7ec`。两份可能包含客体磁盘片段的 QEMU 临时快照未放入公开源码仓库，外部失败原件保持不变。

我方 QEMU PID 18204 于 2026-09-27 21:10:19 UTC 启动。外部 QEMU PID 34880 于 21:10:30 UTC 出现，宿主门禁在 21:10:31 UTC 发现它，随后只停止我方客体。原始外层驱动退出 1、QEMU 退出 -1，`externalLoadObserved=true`；基础盘前后散列相同，冻结输入复核退出 0。串口为 0 字节，客体命令未发送，没有应用运行、中文输入、缩放或公共任务并发结果。一次桌面截图请求遇到已关闭的 monitor，其空日志和后续中断记录保留。

`Review-Interruption.cjs` 已实际退出 0，复核本目录 47 份复制文件的字节数及 SHA-256、外部 QEMU 门禁、退出码、空串口和缺失命令发送记录。随后 `--check-index` 原始退出 0，47 份暂存区 Git blob 与复制清单逐字节匹配。两份完整应用 ZIP 未复制到仓库，原字节仍位于外部冻结输入；本目录不支持独立重建完整只读输入。原始尝试和失败不得重写，后续使用新的尝试编号。`win7CurrentV02Verified=false`，`releaseReady=false`。
