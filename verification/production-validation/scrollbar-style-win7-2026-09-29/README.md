# Win7 SP1 x86：当前 Workbench 滚动条外观

本次使用正式 x86 Release buildId `111abe32-bfed-42aa-ae9c-80dcb3ba2f6e`、源码 SHA-256 `33adadfc3643208c05c27f7ac28c430175d9dcc5be69e254ea8796f8c711dd09`。独立准备并冻结 13 个传输文件，冻结 SHA-256 `06ac9481357e387dbc266a27dea032c25a24c69b93d3521ad3318ca45fcc33c1`；`SouiStarter` 和 `SouiWorkbench` 的完整载荷与各五份 `resources/legal` 文件在客体运行前后核对。没有复用旧构建的 Win7 通过结论。

我方 QEMU PID 45864 使用快照、断网和只读 FAT 传输盘。先截图确认真实 Win7 桌面，随后只发送一次冻结的客体命令。客体确认为 Windows 7 SP1 x86、PowerShell 2，显示模式切换为 1024×768。Workbench React 数据页显示 10,000 行，应用原生退出 0，窗口按原 120 秒界限在约 108 秒由夹具关闭。顺带执行的真实原生缩放将窗口从 `[87,24,937,664]` 调整为 `[87,24,1017,724]`，不作为滚动或输入通过证据。

我检查了[原始屏幕帧](raw/guest/app-check3.ppm)及[PNG 阅读副本](raw/guest/app-check3.png)：页面右缘和万行列表内侧均出现窄蓝灰色滚动条，无旧式上下箭头。复核器还从原始 PPM 像素检查两处滚动条的蓝灰滑块和浅色轨道，避免仅依赖人工标记。这是 Win7 上的**静态外观**观察，不证明滚轮、拖动、键盘滚动是否正确。首次桌面确认元数据写出了格式错误的换行字面量，原文件保留；有效的 `desktop-confirmation-v2.json` 在发命令前另建，没有覆盖旧文件。

串口导出 51 帧、197,644 字节，SHA-256 `4b582a83b5074361f22c8e65a4d26e01e7d66c75e18010d76a102f78cd57d94b`。严格校验器核对每帧长度和散列、客体身份、输入清单、载荷和法律文件、应用 PID/HWND、原生退出与关闭，退出 0。我方 QEMU 与外层驱动退出 0；82 次宿主门禁采样及退出后的终态采样未见其他受控桌面负载。外部 QEMU 于我方驱动完成后才出现；五秒采样仍不能证明绝对隔离。基础盘前后 SHA-256 均为 `fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4`。

`raw/` 保存 39 份输入、客体、屏幕和审查原件；九份含宿主绝对路径的原件留在仓库外，`provenance.json` 保存其长度、SHA-256 及无本机路径的输入/启动字段。大型应用 ZIP、临时快照及基础盘也仅保留仓库外原件和散列。`raw-manifest.json` 的 SHA-256 为 `084b727760de45c2f2c913aad3604fedd08759cbd1dd6f9b25fa67d5ff2a4ba1`，`provenance.json` 的 SHA-256 为 `f153587e4f79d5d6366b509b5813c3aa2cb72e9a9e9c677cc78ba00aa4b85aa6`。复核方式：

```powershell
node verification/production-validation/scrollbar-style-win7-2026-09-29/Verify-Archive.cjs
```

如本机仍保留仓库外原件，可加 `--originals` 和外部根目录逐字节比对；九份未入库原件也会按散列复核。该检查同时比对 Git 暂存区的原始字节。机器审查文件中的 `screenshotReviewed=false` 保持原样，人工视觉观察另记于 `visual-review.json`。真实滚动、中文 IME 组合/提交、DPI、多屏、30 秒公共任务并行、长稳和 Electron 对照仍待；`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。
