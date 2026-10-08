# 安装后公共托盘、菜单与快捷键 SDK 独立消费者

直接 CMake 示例按 3.27 以上准确选择 Windows SDK 10.0.19041.0；3.24–3.26 改用 `-A x64`（x86 为 `-A Win32`），仍保留完整引号的 `-DCMAKE_SYSTEM_VERSION=10.0.19041.0`。已有生成平台不同的目录请选新目录，或检查其所有权后显式 `--fresh`；不能在旧缓存中直接切换平台。

本目录通过安装后的 `find_package(SouiSdk 0.3.0 EXACT)` 和 `Soui::Sdk` 编译，只引用公开头文件。应用自己创建隐藏顶层 HWND 并提供消息循环，调用公共 `tray_service`、`menu_service` 和 `global_shortcut`；不创建 WebView，不依赖模板、Workbench、私有 SDK 头文件或内核 DLL。仍需要对应架构的受支持 MSVC 运行库。preview.10 的输入与行为不同于 preview.9，旧短测不能证明本版通过。

程序必须显式选择一个模式，不接受其他参数：

- 两种模式均安装托盘并注册 Ctrl+Alt+F9（ID 100），再用独立对象和 ID 101 请求同组合，要求 Windows 拒绝且原始错误码为 `ERROR_HOTKEY_ALREADY_REGISTERED`。首次注册被其他应用占用时明确失败，不抢占、不切换组合掩盖问题。
- `--smoke-test`：约 500 ms 后通过公共 SDK 请求中文菜单，由后续计时消息调用公共 `cancel()`，记录无选择返回与清理；随后更新提示，约 1.5 秒后退出；总等待上限 10 秒。它验证调用结果和顺序，不证明图标可见、真实按键或人工 Esc。提前无选择返回但未执行取消请求也判失败。
- `--interactive`：最多驻留 180 秒；通过图标激活、上下文菜单通知或 Ctrl+Alt+F9 打开公共 SDK 菜单，包含中文、子菜单、禁用项与勾选项，可更新提示或移除并退出。超时返回非零，不能视为交互通过。

菜单选择或 Esc 取消后、执行所选动作之前，消费者调用公共 `tray_service::return_focus()`，记录 `tray.focus` 的返回值和状态。超时或退出请求打断菜单时，只要对象存活且图标仍确认安装，也会尝试归还通知区焦点；调用失败则停止后续菜单动作并保持非零退出。无法确认安装时记录 `tray.focus.skipped` 并判失败。此调用不授予可信手势，实际键盘激活、Esc 取消后的焦点位置仍需独立桌面验证。

stdout 输出逐行 UTF-8 NDJSON，包含模式、递增序号、单调耗时、SDK 版本、架构、调用结果、状态及关闭顺序。每次 `TaskbarCreated` 消息分别记录处理结果和 `installed` / `desired` / `cleanupPending`，失败后继续接收后续消息以观察再次恢复；曾发生失败的运行最终仍返回非零。消息源无法由这个接口验证，所以 `sourceVerified=false` / `trustedGesture=false`；日志不能单独证明是实际 Explorer 重启或真实输入。

正常关闭顺序为菜单返回及 `menu.cleanup` → `shortcut.conflict-probe.unregister` / `shortcut.unregister` → `tray.remove` 且状态清空 → 服务析构 → `owner.destroyed` → `owner-class.removed` → `run.finished`。各移除操作失败最多重试三次，仍失败则记录未确认清理并返回非零；曾发生的操作失败不会被重试成功或析构尽力清理改成通过。退出码 0 要求预期冲突以外的全部已执行操作及清理成功；1 表示操作、消息循环、日志或清理错误，2 表示参数错误，3 表示观察超时。外部监督器仍应设置稍大于本程序上限的进程超时，以覆盖系统调用停滞。

## 独立构建

先从交付根运行 `tools/Build.ps1 -SdkOnly -Architecture x64 -Configuration Release`，确认结果成功。以下命令同样从交付根执行；每轮选择新的仓库外目录，不覆盖旧证据。把 `$suite` 改为本机有空间的绝对路径：

```powershell
$suite = Join-Path $env:TEMP ('soui-tray-' + [guid]::NewGuid().ToString())
New-Item -ItemType Directory -Path $suite | Out-Null
Copy-Item -LiteralPath tests/tray-consumer -Destination (Join-Path $suite 'consumer-source') -Recurse
cmake --install out/sdk-x64 --config Release --prefix "$suite/install"
if ($LASTEXITCODE -ne 0) { throw 'SDK install failed' }
cmake -S "$suite/consumer-source" -B "$suite/build" -G 'Visual Studio 17 2022' -A 'x64,version=10.0.19041.0' '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' "-DSouiSdk_DIR=$suite/install/lib/cmake/SouiSdk"
if ($LASTEXITCODE -ne 0) { throw 'Independent consumer configure failed' }
cmake --build "$suite/build" --config Release --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Independent consumer build failed' }
```

x86 使用其独立安装包 `out/sdk-x86`、`-A 'Win32,version=10.0.19041.0'`（3.24–3.26 为 `-A Win32`）和相同的 `'-DCMAKE_SYSTEM_VERSION=10.0.19041.0'`，保留另一套新目录。不能把一个架构的安装结果用于另一架构。

## 本地验收边界

桌面空闲时串行运行构建得到的 `bin/Release/SouiTrayConsumer.exe`，显式传入上述一个模式，并由外部监督器保存原始 stdout、stderr、进程退出码和源码/安装 SDK/EXE 散列。短测成功后仍要另做图标实际可见、中文提示、鼠标与键盘通知、真实 Explorer 重启、恢复失败与后续重试、退出后图标消失以及 Windows 7 SP1 x86 的桌面验证。

本消费者不发送模拟托盘或快捷键消息、不主动重启 Explorer，也不声称任何消息具备授权效力。它覆盖三项原生服务的独立接入；实际菜单选择、Esc 后焦点、真实快捷键、三窗口业务隔离、无窗口驻留策略和长期资源回收仍需各自桌面证据。加入源码或编译成功均不代表这些门槛通过。
