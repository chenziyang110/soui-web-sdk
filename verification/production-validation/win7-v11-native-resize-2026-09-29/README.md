# Win7 SP1 x86 V11 原生工作区缩放（2026-09-29）

本次沿用未改写的 13 文件输入 `win7-v11-transfer-attempt1`，冻结 SHA-256 为 `6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2`，绑定正式 x86 Release buildId `b6caec6c-117e-49eb-86fa-4182e64d5b55` 和源码 SHA-256 `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640`。前两次因外部 QEMU 中断的原始记录仍分别保存在 [启动前拒绝](../win7-v11-prelaunch-refusal-2026-09-29/README.md)与[命令后中断](../win7-v11-command-interrupted-2026-09-29/README.md)，没有覆盖。

第三次 `guest-native-resize-v11-attempt3` 在宿主空闲后启动我方 QEMU PID 36196。首张截图显示 Win7 欢迎页；随后截图确认真实桌面，才于 `2026-09-29T06:09:50.497Z` 向监视器**发送一次**冻结命令。客体为 Windows 7 SP1 x86，显示模式 1024×768。严格校验器核对 54 帧、200,483 字节串口、客体身份、输入、完整载荷与 `resources/legal` 前后散列，原始退出 0。Workbench React 数据页应用 PID 260、HWND 327986；原生探针在自然前台记录初始及拖拽前矩形 `[87,24,937,664]`，一次真实 80×60 鼠标拖拽后及复查矩形均为 `[87,24,1017,724]`，应用原生退出 0、窗口按夹具关闭。此证据只证明当前构建在这一 Win7 显示条件下，普通 Workbench 的初始边框可触及并能原生放大 80×60。

我检查了原始 [拖拽后屏幕帧](raw/desktop-aftercommand-2.ppm) 及 [PNG 阅读副本](raw/desktop-aftercommand-2.png)：Workbench 数据页可见，放大后的右下边框在屏幕内、任务栏上方；原始 PPM 的 SHA-256 为 `076f4f4bc3fa5865be26d3fdc712a2ff1453bad5ce3c11615c84bcc2aa0c108f`。截图只辅助核对显示状态，精确矩形来自客体原生探针；自动校验输出中的 `screenshotReviewed=false` 保持原样，未回填成人工通过字段。

我方 QEMU 与外层驱动均原始退出 0，无超时。95 次宿主采样及退出后终态采样未见其他受控桌面负载；串口结束后才向**我方** QEMU 发送 `quit`。基础盘前后 SHA-256 均为 `fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4`，冻结输入复核退出 0。QEMU stderr 的 TCG CPUID 提示保留原样。客体时钟与宿主时钟不能直接比较；此次不生成跨时钟启动、帧或响应性能结论。

[43 份原件](raw)包含输入/驱动/校验器身份、串口、门禁、原始 PPM 与 PNG、监视器回执及退出码，未复制大型应用 ZIP、临时快照或基础盘。`raw-manifest.json` 的 SHA-256 为 `f4bd57e52a6014734dbc3135b7ebc2318b0bbc2545c5d33c2b693792de6c06fb`，固定每份原件的长度、SHA-256 和仓库外相对来源。`node verification/production-validation/win7-v11-native-resize-2026-09-29/Verify-Archive.cjs` 核对原件与 Git index；加 `--originals <EXTERNAL_ROOT>` 时还比对仓库外原件。[实际复核结果](origin-review.result.json)原始退出 0、stderr 空，三份复核日志与仓库外原件及 Git index 字节相符。完整原件保留在 `C:\SOUI-SDK-production-validation\work-area-placement-20260928\guest-native-resize-v11-attempt3`。

这**不**证明真实中文 IME 组合、列表滚动与公共 SDK 任务并行、800×600 可用性、DPI/多屏、长稳或 Electron 对照。`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`；[Issue #42](https://github.com/chenziyang110/soui-web-sdk/issues/42) 保留至相关 Win7 范围复核。
