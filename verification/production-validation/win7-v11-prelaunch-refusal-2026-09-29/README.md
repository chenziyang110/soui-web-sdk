# Win7 V11 原生缩放客体启动前拒绝（2026-09-29）

本次使用已冻结的 `win7-v11-transfer-attempt1`，冻结文件 SHA-256 为 `6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2`。输入准备、冻结和只读验证各自原始退出 0；13 个冻结文件与 x86 正式构建身份保持不变。本目录只归档第一次客体启动尝试的非专有原件。

驱动开始时宿主无其他桌面或 QEMU 负载。校验冻结输入和基础盘期间，外部 QEMU 于 `2026-09-29T04:59:59Z` 启动；第二次启动前门禁在 `05:00:16Z` 检出该进程并拒绝启动我方虚拟机。驱动原始退出 1，`launch.json`、`command-send.json`、串口导出和客体结果均不存在。因此没有发送命令、没有 Win7 应用运行结果，不能把此次当作兼容性失败或通过。原门禁、冻结检查、脚本快照和驱动 stdout/stderr/退出码保留在 [raw](raw)。

[raw-manifest.json](raw-manifest.json)固定这些原件的字节数与 SHA-256；`node verification/production-validation/win7-v11-prelaunch-refusal-2026-09-29/Verify-Archive.cjs` 对照工作区和 Git index。加 `--originals` 时还逐字节比对仓库外原件，本次 [比对结果](origin-review.json)原始退出 0、stderr 空。完整转移输入及其三个安全捕获尝试留在 `C:\SOUI-SDK-production-validation\work-area-placement-20260928`，不复制大型应用 ZIP。下一次客体必须使用新的 `guest-native-resize-v11-attempt2`，保持同一冻结输入并重新确认真实 Win7 桌面后只发送一次冻结命令。V11 真实缩放、中文 IME、任务并行和生产门槛仍未通过；`win7CurrentV02Verified=false`、`releaseReady=false`。
