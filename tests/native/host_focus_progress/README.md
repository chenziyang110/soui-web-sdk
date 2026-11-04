# 实际 Host 焦点进度回归

本夹具链接当前候选 SDK，加载调用者冻结的真实 Miniblink `node.dll`，挂载公共 `soui_miniblink_host` 和 `dynamic_miniblink_adapter`。它验证实际 Engine 被独立任务占住时，UI owner 的公共 `focus()` 仍能完成原生子窗口焦点切换并返回；之后再释放 Engine，确认排在焦点任务之后的 FIFO fence 完成。它不是 fake adapter、纯函数模型或运行帧率测试。

这个候选测试要求新 frame policy capability。通过公共 `application_diagnostics()` 验证 `frame_negotiated=true`、`frame_applied_hz=60` 和普通 `json_bridge` 的 `frame_feedback_supported=false`。这证明当前候选 SDK/DLL 实际接线和旧 Bridge 扩展点的回退，不证明同步显示节拍或物理 FPS。基于 0.6.0 源码的候选改动不等于已发布 0.6.0 载荷。

## 桌面和生命周期边界

`SetFocus` 可能激活目标的父窗口；仅使用隐藏窗口或 `WS_EX_NOACTIVATE` 不能证明不会影响当前桌面。因此控制器创建唯一的非输入 Win32 desktop，将它通过 `STARTUPINFO.lpDesktop` 传给子进程，并检查与当前 input desktop 的名称不同。全过程不调用 `SwitchDesktop`、`SetForegroundWindow`、`SendInput` 或 UIA，不回退到用户桌面。实际原生 `SetFocus` 只在私有 desktop 中发生。[Microsoft SetFocus 契约](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus)

控制器持有 desktop 和仅包含本次子进程的 Job。创建 desktop、事件、Job 或挂载失败均返回非零，不能记为通过。Engine 的 hold callback 等待控制器事件，并另有 10 秒保险超时；控制器不运行在 Engine 线程上。它在启动后最多等 30 秒进入焦点阶段，在焦点阶段最多等 2 秒，超时先写失败证据，再尝试有独立 3 秒时限的 minidump helper，然后释放 Engine。超时后的恢复退出不会把原失败改成通过。子进程最多再有 25 秒正常清理，超限只终止本次 Job 并保留失败。自然退出后还必须在 3 秒内通过 Job accounting 确认所有后代均退出；不能让 Job 析构时的强制清理被记为自然成功。

worker 先以 ES5 页面执行真实 bootstrap、带相同 session/origin/token 的 `bridge.probe.report` 和自定义校准请求，确认 document/application ready。身份信息不写入报告。开始 hold 前检查已加载 DLL 的实际 module path 和 SHA-256，并把原生焦点移到本次 owner，避免公共 `focus()` 仅命中已经聚焦的子窗口。hold 期间不调用可能等待 Engine 的诊断/就绪查询。所有异步 fence 句柄使用共享所有权直到任务结束。

## 构建和执行

从仓库根目录运行；下面路径是相对路径示例，先按本次冻结的构建和运行载荷调整。SDK 库必须与当前头文件、架构、宏布局和 Release CRT 匹配。运行目录必须为任务所有的副本，不能覆盖已发布包；将候选 DLL 复制为该目录的 `node.dll` 后刷新测试清单并冻结散列。

```powershell
cmake -S tests/native/host_focus_progress -B ../focus-build-x64 `
  -G 'Visual Studio 17 2022' -A 'x64,version=10.0.19041.0' -T v143 `
  -DMATCHED_SDK_LIBRARY="$((Resolve-Path out/pr129-native-x64/sdk/Release/soui_sdk.lib).Path)"
cmake --build ../focus-build-x64 --config Release --parallel 2
$focusRuntime = (Resolve-Path ../focus-runtime-x64).Path
$focusPage = (Resolve-Path ../focus-build-x64/resources).Path
$focusOutput = [IO.Path]::GetFullPath('../focus-result-x64-r1')
$focusHash = (Get-FileHash -LiteralPath (Join-Path $focusRuntime 'node.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
& ../focus-build-x64/Release/soui_host_focus_progress.exe $focusRuntime $focusPage $focusOutput $focusHash
if ($LASTEXITCODE -ne 0) { throw 'Mounted focus progress failed; keep evidence.' }
```

输出目录必须不存在，父目录必须已存在。参数要求绝对路径。夹具不注册默认 CTest，以免在未冻结 DLL、未指定运行载荷时误报成功；编译成功不代表运行成功。

## 证据与限制

- `controller-result.json`：独立 watchdog、桌面隔离、返回先于 release、真实退出；失败时另有 `watchdog-failure.json` 和尽力生成的 `watchdog-failure.dmp`。
- `worker-result.json`：真实 Bridge 校准、实际 DLL path/hash、候选 frame capability、公共 focus 结果、native child focus、FIFO fence、销毁和全局 runtime 退出。
- `worker-stages.ndjson`：阶段、QPC、PID、TID。报告必须连同源码状态、SDK 库、fixture、页面、完整运行目录清单和命令退出码保存；单独一个成功 JSON 不能绑定完整候选身份。

通过只证明这次架构、候选 SDK/DLL 组合在私有非输入 desktop 中的实际挂载焦点路径不等待 held Engine，以及所列的能力协商。2 秒是死锁/同步等待回归 watchdog，不是输入延迟性能目标。可见窗口、真实鼠标键盘、IME、重入关闭/导航、DPI/多屏、FPS、输入到显示的延迟仍需目标桌面的独立验收；本夹具始终记录 `performanceVerified=false`。
