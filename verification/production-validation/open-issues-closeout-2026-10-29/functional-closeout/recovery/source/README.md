# 独立托盘 Explorer 恢复短测

本程序独立引用已安装的公共 `SouiSdk 0.6.0 EXACT` / `Soui::Sdk`，只验证真实 Explorer 重启后的托盘恢复，以及显式移除后再次重启不复活。它不启动 SDK 内核或 WebView，不包含菜单、快捷键、焦点归还或 Explorer 重启代码。已有 `tests/tray-consumer` 保持独立。

在仓库外构建；使用支持的 MSVC v143、Windows SDK `10.0.19041.0`，将 `CMAKE_PREFIX_PATH` 指向对应架构已有安装前缀。例如由仓库根执行：

```powershell
cmake -S tests/tray-recovery-consumer -B ../tray-recovery-x64 -G "Visual Studio 17 2022" -A "x64,version=10.0.19041.0" -DCMAKE_PREFIX_PATH=<installed-x64-prefix>
cmake --build ../tray-recovery-x64 --config Release --target SouiTrayRecoveryConsumer
```

上例适用于 CMake 3.27 及以后；x86 使用独立目录、`-A "Win32,version=10.0.19041.0"` 与 x86 安装前缀。CMake 3.24–3.26 使用 `-A x64`（x86 为 `-A Win32`），并显式传入 `-DCMAKE_SYSTEM_VERSION=10.0.19041.0`，核对生成结果确实选择该 SDK。无需内核运行 DLL；依赖对应 CRT。该目标明确选择构建，不加入默认测试集合。

启动后全部步骤共用一次新的 90 秒绝对截止时间，阻塞 `GetMessageW` 配合 50ms `WM_TIMER`。每次消息分派前检查截止时间，不重试、不刷新预算，也不注入 `TaskbarCreated`。

1. 显示原生 owner/control 窗口，通过公共 `tray_service` 安装唯一 owner/id73 图标；两个按钮均禁用。外部受控操作员实际重启 Explorer 一次。
2. 收到第一次广播后，程序检查旧 Explorer 保留句柄已退出、Shell 新 PID/出生时间身份不同且镜像路径一致，再调用公共消息分派，要求 `handled/ok/installed/desired` 成功且无待清理状态。随后才启用“移除图标”。
3. 点击“移除图标”，只调用一次 `remove()`，要求 installed/desired/cleanupPending 全部为 false。外部操作员第二次真实重启 Explorer。
4. 第二次广播检查新旧 Explorer 身份，再分派消息，要求三项托盘状态仍全部为 false。固定等待 500ms，确认状态和 Explorer 身份保持，才启用“结束验证”。点击后销毁服务、owner 和保留的进程句柄，正常退出。

额外或阶段不符的广播、越阶段按钮命令、查询失败、早关窗口、超时、日志失败或清理失败均非零退出。查询失败记录为错误/未知，不推断不支持的 OS。90 秒预算同时覆盖启动、两次重启、交互和清理；外部监督预算另由执行者保持。

stdout 为 UTF-8 NDJSON：递增 sequence、elapsedMs、UTC FILETIME、runId、PID/出生时间、版本/架构、阶段、每次公共 SDK 调用结果、托盘状态、Shell_TrayWnd 对应 Explorer PID/出生时间/镜像路径、旧进程退出观察、最终清理和退出结果。64 位 FILETIME 使用字符串。日志只随固定阶段/失败/清理产生，不逐 timer tick 写入；防御上限为 128 行，触及上限记录终态日志错误并非零退出。第一次异常事件留痕后停止继续处理阶段输入，等正在执行的 SDK 调用返回再清理。旧 Explorer SYNCHRONIZE 句柄保留到最终清理，防止 PID 重用混淆。

`TaskbarCreated` 来源始终记录为 `sourceVerified:false`。消息及进程身份观察不能替代外部真实重启操作记录；验收必须将外部重启证据与本次身份、原始日志和退出结果绑定。本程序不验证通知区域焦点、Win7 兼容性或完整 Issue/生产资格，也不把编译成功视为上述行为通过。
