# 当前 V02 GUI 输入验收准备

状态：**仅准备，未编译输入探针、未启动当前 Demo GUI、未执行真实交互**。本目录不增加 V02 或生产门槛的通过项。

当前 Demo 的 718 文件构建输入 SHA-256 为 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`。正式 x64/x86 Release 构建 ID 分别为 `5ddcec93-d244-456c-877b-1bdeabee568d` / `f0c38056-058a-456e-be99-17446127f7ec`，对应[构建原件](../v02-continuous-task-official-build-2026-09-27/README.md)。旧 Win7 或旧 Demo 短测不能继承给这份源码。

`raw/v02-gui-preflight-20260927.json` 只记录宿主具备 `zh-Hans-CN` 输入法、应使用 `--react --page=data` 启动，以及页面可观察节点。它的 `guiExecuted` 明确为 false。

`raw/V02InputProbe.cpp`、配套 CMake 定义和说明是**尚未编译**的外部验收辅助程序。设计上仅在目标 PID 的创建 FILETIME、可见顶层 HWND 和自然前台状态核对后发送 Win32 鼠标或普通键盘事件；输入点被其他窗口遮挡时拒绝，绝不强抢前台。输入法组合必须再通过实际页面状态和截图核对；辅助程序固定记录 `imeCompositionVerified:false`。窗口缩放使用边框拖拽并检查两个尺寸变化。它只产生输入动作证据，不会单独证明 V02 页面成功、物理设备兼容、帧呈现或生产可用。

准备时宿主仍有独立 QEMU `82992`（2026-09-27 17:31:15 +08 启动）活动，因此没有并行编译或桌面试验。空闲后先编译并验证辅助程序的拒绝路径，再串行做当前 x86/x64 应用三入口短测和 V02 真实滚动、中文 IME、缩放、公共 SDK 任务并行。保留每次失败原样本，不覆盖旧记录。`raw-manifest.json` 逐一绑定四份准备原件的字节与 SHA-256；`releaseReady=false`。
