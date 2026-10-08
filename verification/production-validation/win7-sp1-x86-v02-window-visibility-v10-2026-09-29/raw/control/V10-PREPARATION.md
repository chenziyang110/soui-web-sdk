# Win7 V10 可见窗口时序诊断预备

V9 客体 `guest-work-area-v9-attempt1` 在冻结的 Win7 SP1 x86 上切换为 1024×768 并启动当前 x86 Release Workbench，但 10–50 秒的九次可见顶层窗口探测均失败；约 79 秒的宿主截图也未显示应用。客体约 90 秒由脚本请求关闭，整轮退出 1。69 份原始文件和严格失败审查已复制到工作区的 `verification/production-validation/win7-sp1-x86-v02-workarea-v9-failure-2026-09-29`。V9 输入和结果不可覆写。

V10 是只读诊断，不是修复或产品验收。`win7-v10-source/run-visibility-diagnostic.ps1` 在同一 x86 Release 应用、1024×768 和原有 120 秒应用期限内，从约 10 秒开始每 5 秒尝试可见窗口探测到 105 秒，并在每次记录进程 `MainWindowHandle`、Smoke 日志阶段和探针原始输出。约 108 秒请求关闭；不调用滚轮、鼠标、拼音或缩放动作。它用于区分晚启动与窗口始终不可见，不证明工作区修正、IME、任务或性能。

历史对照仅用于解释诊断采样窗口：`win7-sp1-x86-v02-visible-page-2026-09-28` 的原始串口中，可见窗口探测在 10、15、20、25、30 秒为失败，约 35 秒首次成功；那轮绑定不同源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`、buildId `f0c38056-058a-45be-be99-17446127f7ec`，启动宽高为 700×500。当前 V9 绑定源码 SHA-256 `60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640`、buildId `b6caec6c-117e-49eb-86fa-4182e64d5b55`、850×640，至 50 秒仍未检出；其原始 `close-attempt.txt` 在约 90 秒记录非零 `MainWindowHandle=196766` 和 `CloseMainWindowPosted=True`，但没有 50–90 秒之间的同一探针采样，不能从关闭记录推断首次可见时间。两轮构建和窗口尺寸不同，不能将历史 35 秒当成当前版本时延基线，也不能据此推断根因。

静态输入 SHA-256：客体脚本 `585f5c614fda1cbd7a9d183013da0c15a0cd88de8bd61712eaabd304f15cd84a`；前一轮冻结 V9 输入 `98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80`。`v10-powershell-parser.json` 是 Windows PowerShell 5.1 实际解析三份 PowerShell 脚本的原始记录，错误和应用输入调用均为 0；`Validate-V10Toolchain.cjs` 实际退出 0，核对六份控制文件字节、Node 语法、前一轮冻结输入和解析记录。它不等于在 Win7 PowerShell 2 上执行。`v10-static-preparation.json` 标记 `transferFrozen=false`、`guestExecuted=false`。

宿主与其他 QEMU/编译/桌面负载空闲后，使用未用过的 `win7-v10-transfer-attempt1` 顺序运行 `Prepare-V10Transfer.cjs`、`Freeze-V10Transfer.cjs`、`Verify-V10Transfer.cjs`，逐步保存原始退出码、stdout/stderr、输入和载荷散列。三者均成功后读取新的 `freeze.json` SHA；不得沿用 V9 冻结 SHA 作为 V10 运行参数。正式客体名称 `guest-window-visibility-v10-attempt1` 尚未使用，直接用 `Run-V10.ps1` 的 `-Attempt`、`-InputName`、`-ExpectedFreezeSha256` 参数启动。`Launch-V10.ps1` 自带运行中外部负载门禁，只停自身 QEMU；不要用 `Capture-IsolatedBuild.cjs` 包裹客体，因为它会把自有 QEMU 错认成外部负载。

客体启动后截图确认真正的 Win7 桌面，再用同一 `Monitor-DisplayExplore.cjs` 只发送一次 `cmd.exe /c D:\soui-input\run-display-explore.cmd`。保留启动/过程/结束截图、串口、客体身份、原生退出码、基础盘与冻结输入前后身份。只有读取完整原始证据后才能编写 V10 成功或失败审查；不要以 QEMU 退出 0 代替应用窗口结论。V10 即使找到晚启动窗口，也不关闭 V02 真实交互和生产门槛。
