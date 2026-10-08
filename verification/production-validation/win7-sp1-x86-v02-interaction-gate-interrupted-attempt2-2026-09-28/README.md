# 当前 V02 Win7 SP1 x86 交互尝试第二次被外部负载中断

第二次使用原 13 文件只读冻结输入，SHA-256 `4a8eb139db54d993821da4e95a63f9100e14c3d3d91fab7afbdc34a6dad5e8a3`，绑定 718 文件 Demo 源码 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 和正式 x86 Release 构建 `f0c38056-058a-456e-be99-17446127f7ec`。启动前实际冻结核查退出 0，外部负载门禁为空。

我方 QEMU PID 31308 于 2026-09-27 21:34:42 UTC 启动。首次原始桌面截图仍是 Win7 欢迎界面，故未发送客体应用命令。外部 QEMU PID 75656 于 21:35:12 UTC 出现，宿主门禁于 21:35:16 UTC 发现并只停止我方客体。外层驱动退出 1、我方 QEMU 退出 -1；客体串口 0 字节，`command-send.json` 不存在，应用没有运行。基础盘前后 SHA-256 一致，冻结输入前后核查均退出 0。后续截图请求因 monitor 已关闭而失败，错误原始日志保留。

`raw/` 存放 19 份复制原件，包括驱动原始退出记录、全过程门禁、客体启动信息、欢迎界面 PPM/PNG 与串口。`Archive-and-Review.cjs` 的 `archive` 和 `review` 实际退出 0，复核复制件与外部原件的大小、SHA-256、外部 QEMU、基础盘、空串口及未发命令；暂存后再执行 `--check-index` 比对 Git blob。两份可能含客体磁盘片段的 `vl.*` 临时快照保留在外部原目录，不进入公开仓库。两份应用 ZIP 仍只在外部冻结输入；本目录不能独立重建完整只读输入。

随后的源码清单刷新尝试也遇到新出现的外部 QEMU PID 81132：实时门禁只停止我方子进程，原始退出 1，stdout/stderr 为空，原 `source-manifest.json` 没有变化。四份原始驱动/门禁记录另存 `raw/manifest-interrupted/`，`Archive-ManifestInterruption.cjs` 复制和只读复核均退出 0。此次清单刷新不算通过，待空闲时使用新尝试编号重新生成并验证。

新一次空闲窗口中，清单生成实际退出 0，输出 36,840 个文件；紧随其后的清单验证被新出现的外部 QEMU 打断，原始退出 1、无验证结果。两步的八份原始命令/门禁记录存于 `raw/manifest-refresh/`，`Archive-ManifestRefresh.cjs` 复制与只读复核退出 0。当前工作树的清单已变更，但在完整验证成功前不把它算作交付通过。

下一次清单验证在空闲环境中实际退出 1，原始错误为 `Source file set differs from manifest`。核对 Git index 与清单发现，清单生成后新增了本目录的十份归档文件，旧清单 36,840 项、当前暂存文件集 36,850 项；这属于输入集合变化，不是源码字节损坏。四份原始验证/门禁文件另存 `raw/manifest-set-mismatch/` 并由 `Archive-ManifestSetMismatch.cjs` 只读复核。完成本次证据暂存后必须重新生成和验证清单，不改写这些失败记录。

随后 `source-manifest-interruption2-attempt3` 因另一台 QEMU 出现而由实时门禁终止，原始退出 1，未改变旧清单；原件留在外部验证目录。空闲后 `source-manifest-interruption2-attempt4` 生成 36,856 项清单，`verify-manifest-interruption2-attempt3` 对当时的暂存输入逐项核验，两项驱动原始退出均为 0、无外部负载干扰。日志位于外部 `preview15-v02-interaction-20260927/command-logs/` 的同名目录。由于本说明随后更新，提交前还须对最终暂存文件集重新生成并验证清单；中间成功结果不跨输入集复用。

源码清单仍须以最终 Git 暂存输入重新生成并独立验证，不跨输入集复用成功结果；各次原始命令记录保留在外部 `preview15-v02-interaction-20260927/command-logs/`。清单结果不改变上述客体中断结论。

这次不是应用兼容性通过或失败。下一次必须使用新的尝试编号，先确认真实 Win7 桌面，再发送一次冻结命令。`win7CurrentV02Verified=false`，`releaseReady=false`。
