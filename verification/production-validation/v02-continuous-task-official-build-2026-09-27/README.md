# V02 持续任务：正式应用构建

状态：**x64 Release 正式构建已通过；x86、真实 GUI 与 Win7 SP1 x86 尚未执行**。本目录只记录当前 Demo 源码的应用构建证据，不更新生产就绪结论。

2026-09-27 的 `official-x64-attempt1` 使用仓库正式 `tools/Build.ps1 -Architecture x64 -Configuration Release`，包含完整 Web 构建。外层驱动与正式构建原始退出码均为 0。构建前后 718 文件源码 SHA-256 都是 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`；构建 ID 为 `5ddcec93-d244-456c-877b-1bdeabee568d`。Starter 完整载荷为 71 文件，SHA-256 `f2b5acea78d3baa8e34492c4264e4a36ef7f38e6d18a2073de144398278eff9d`；Workbench 为 475 文件，SHA-256 `48ec1c2261bef39a45c2e95bff51b8313b50778f5d6ca4af51254fa88859c308`。

构建前后负载门禁分别在 06:03:35Z、06:04:50Z 记录为空。另一台 QEMU 在 06:04:53Z 才启动；其运行期间没有继续 x86 构建。本目录 `raw/` 保留驱动源码、门禁、源码与载荷清单、原始标准输出/错误及退出码。`raw-manifest.json` 对拷贝后的原始文件逐个记录 SHA-256。先前 native 回归与 RED 样本分别保留在 `v02-continuous-task-native-2026-09-27` 和 `v02-continuous-task-preparation-2026-09-27`，不能据此推断本次应用的 GUI 行为。

下一步宿主空闲后以新名称执行 x86 Release `-SkipWeb`，并为当前构建建立本机和 Win7 SP1 x86 的真实滚动、中文 IME、缩放与公共任务并行验收。`releaseReady=false`。
