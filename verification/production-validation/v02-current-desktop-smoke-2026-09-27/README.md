# 当前 V02 应用双架构本机三入口短测

状态：**当前 718 文件 Demo 源码的 x86/x64 Release 三入口短测通过；V02 数据页真实输入、Win7 x86 和长期稳定性仍未执行**。不更新 `releaseReady=false`。

外部一次性等待器 `queued-smoke-attempt1` 在 2026-09-27 09:54:10Z 启动，只做严格 CIM 空闲查询；三次空闲采样后，按 x86 再 x64 的顺序启动 `Run-CurrentApps.cjs`。x86 子进程 09:54:22.638Z–09:54:35.279Z、x64 09:54:35.916Z–09:54:46.077Z 均原始退出 0。两次运行各保存启动前、过程中和结束后的负载门禁，均无外部桌面/编译/QEMU 负载或查询错误；另一台 QEMU 于约 09:55:02Z 才出现。等待器终态 `completed-two-architecture-smoke`，自身进程已退出。

两架构 `build-before.json` 与 `smoke.json` 分别绑定当前 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`。x86 buildId `f0c38056-058a-456e-be99-17446127f7ec`，x64 buildId `5ddcec93-d244-456c-877b-1bdeabee568d`；Starter/Workbench 完整载荷散列与[正式构建记录](../v02-continuous-task-official-build-2026-09-27/README.md)逐项一致。运行器在每个短测结束后重新核验源码和构建载荷。

每架构的 Starter 默认、Workbench 默认、Workbench React 三项原生退出码均为 0，`smoke.json` 标记全部通过，原生阶段日志含 `application-ready` 与 `host-close-complete`，均无 45 秒单项超时。六项合计覆盖当前应用的短启动和完整关闭。`tools/Test.ps1 -NativeOnly` 以隐藏窗口执行，未进入 `--page=data`，没有滚动、中文 IME 组合、边框缩放或后台任务并行；不能把三入口短测写作 V02 真实交互通过，也不能替代 Win7 SP1 x86 或隔离长稳。它不证明物理设备、帧呈现或 Electron 性能。

`raw/` 保存 28 份原始驱动、门禁、源码/载荷身份、标准输出/错误、退出码和 smoke 报告；`raw-manifest.json` 逐一列出字节与 SHA-256。28 份原件、工作区和 Git index 已逐字节核对一致。此前准备文件见[GUI 输入验收准备](../v02-current-gui-preparation-2026-09-27/README.md)；输入探针仍尚未编译或运行。
