# Win7 V11 原生命令后受外部负载中断（2026-09-29）

本次沿用已冻结的 `win7-v11-transfer-attempt1`，冻结文件 SHA-256 为 `6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2`，未改写输入。宿主起始与启动前门禁均为空闲；我方 QEMU PID 64048 于 `05:11:32Z` 启动。原始屏幕帧显示 Win7 桌面，人工确认后于 `05:14:34Z` 通过监视器发送唯一一次冻结命令 `cmd.exe /c D:\soui-input\run-display-explore.cmd`。后一帧可见命令提示符窗口，但不能从图像推断客体脚本完成。

外部 QEMU PID 21428 于 `05:15:55Z` 启动，实时采样在 `05:15:56Z` 检出。门禁只停止我方虚拟机；驱动原始退出 1、我方 QEMU 退出 -1。串口导出为 0 字节，缺少客体身份、原生窗口操作、应用退出和资源/legal 的客体结果，因此不能判定 Win7 工作区缩放通过或失败，也不能作为隔离资源或时延结论。冻结输入检查原始退出 0，基础盘前后 SHA-256 均为 `fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4`。

[25 份原件](raw)保存了门禁时间线、启动与命令身份、监视器回执、原始 PPM/便于查看的 PNG、空串口、QEMU 与驱动退出及输入复核；未归档 QEMU 大型临时快照和应用 ZIP。`raw-manifest.json`固定每份归档原件的长度与 SHA-256。`node verification/production-validation/win7-v11-command-interrupted-2026-09-29/Verify-Archive.cjs --originals` 同时逐字节核对仓库工作区、Git index 与仓库外原件；本次[复核](origin-review.json)原始退出 0、stderr 空。不带 `--originals` 可在没有外部目录时复核。PNG 是从对应 PPM 转换的阅读副本，不能取代原始 PPM。完整外部失败样本仍保存在 `C:\SOUI-SDK-production-validation\work-area-placement-20260928\guest-native-resize-v11-attempt2`，不得覆盖。

下一次客体必须使用新的 `guest-native-resize-v11-attempt3` 和独立驱动日志目录，复核同一冻结输入并在真实桌面确认后只发送一次命令。V11、真实中文 IME、任务并行、Win7 当前 V02 与生产门槛仍未通过；`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。
