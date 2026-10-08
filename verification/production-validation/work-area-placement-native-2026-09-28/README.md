# 普通窗口工作区定位：当前源码的正式构建与无窗口回归

旧候选在 Win7 SP1 x86 的 1024×768 桌面出现[真实缩放失败](../win7-sp1-x86-v02-interaction-v7-resize-blocked-2026-09-28/README.md)：默认窗口下边框被任务栏覆盖。本分支只修改 Workbench 普通窗口的初始位置与尺寸，优先在主显示器工作区保留向右下缩放的空间；SDK Core 不变。该修正仍需当前源码的现代 Windows 和 Win7 原生 GUI 输入验收，Issue #42 继续开放。

源码输入 SHA-256 为 `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640`。x64 正式 `Build.ps1` Release 全 Web 构建 ID 为 `c8d857b1-0083-40bf-ae3a-3b5ed5f4ad47`，x86 正式 Release `-SkipWeb` 构建 ID 为 `b6caec6c-117e-49eb-86fa-4182e64d5b55`；两次驱动和实际构建均退出 0，构建前后源码散列相同，完整 Starter 71 文件和 Workbench 475 文件载荷核对通过。两架构各实际执行一次 `demo.native.window-placement`，均为 1/1 测试通过、原始退出 0，测试 EXE 前后散列不变，前后负载门禁为空。

第一次 x64 驱动调用在编译前因外部日志父目录不存在而报 `ENOENT`，没有产生该次命令日志，也没有启动编译器；修正驱动只补建父目录后改用新的 `workarea-x64-attempt2`。这项启动失败根据终端返回记录，不能算作原始归档命令。正式构建及测试的[原始日志、门禁、源码/构建/载荷身份与驱动快照](raw-manifest.json)另行逐字节复制；`node Archive.cjs verify` 核对副本、存在时的外部原件和实际结果，暂存后 `node Verify-Index.cjs` 核对 Git index。

这些是编译与纯几何回归，不证明真实窗口创建位置、任务栏遮挡、鼠标缩放、页面在 708×488 下可用、中文输入或生产能力。`guiVerified=false`、`win7Verified=false`、`releaseReady=false`。
