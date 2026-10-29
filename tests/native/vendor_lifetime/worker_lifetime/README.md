# Worker 生命周期真实源码回归

## 2026-10-29 deadline 调度候选

当前主线程改为 ready 合并消息和最近 deadline 单次 threadpool timer；空闲不再产生周期 WM_TIMER。`main-thread-compatibility` 因此改为检查初始无周期 timer、真实 immediate task、不得提前的 25ms delayed task、deferred 后取消/重 arm 的恢复、旧 WM_TIMER 票号拒绝、WM_QUIT 不受自补消息饥饿，以及实际 HWND/类/任务释放。Win32 threadpool timer 仍为真实 API，fixture 未替换它的调度。平台时间声明增加单调时钟投影；原 worker/关闭故障断言保持。源码和 harness 身份重新绑定不代表新 Native 测试已执行，旧结果不能外推本候选。

## 当前源码绑定修复（2026-10-13）

0.5 Native 源码采用后，本目录的 current 输入仍绑定采用前 5 个文件及 3 个旧字节偏移，导致正常配置被 byte gate 拒绝。更新时逐项验证完整文件，并以原片段 SHA 定位相同方法；registry/TLS 的三个片段原字节不变，仅位置移动。完整 WebThreadImpl 则包含已经采用的 retirement/witness 方法，测试依赖投影从真实 WebThread.h 额外提取该接口声明，不能手写另一套关闭实现。新增第 12 个 exact fragment，严格 copies 数同步为 12；13 个 current 源输入与所有 payload/HEX 前后校验仍保留。

此次只修测试侧 current 来源和必要声明，原 12 个行为断言、故障包装、时间预算及下文历史实测原件保持。其他 vendor 子目录分别核对 current 与派生片段，不能把全文件 hash 更新替代 fragment 对齐。当前只有文件/Node 数据检查，未执行 CMake、编译或 Native 回归，也不继承旧 2026-10-09 结果。

本目录提供一个 Windows/MSVC C++17 测试程序和 12 个 CTest。它从当前 Miniblink 源码生成输入，覆盖 worker 构造、启动交接、真实线程退出、注册撤销及主线程计时器行为。测试目录不保存历史 before 快照，也不包含开发机绝对路径。

`source-map.json` 固定 13 个当前源码输入的完整 SHA-256/字节数，以及 12 个生成输入的精确字节区间。完整文件按原字节复制；片段通过 HEX 解码写入，不做变量、美元符号、生成表达式或换行替换。片段必须为 NUL-free、LF-only 且保留末尾 LF。配置时和编译前都验证完整源码，随后逐个比较生成内容与真实源码区间；改变源码必须重新审查和更新映射，不能改成手写生命周期算法。

程序编译真实 `WebThreadImpl`、`WebTimerBase`、`SharedTimerWin` 和平台注册/TLS、scheduler 构造析构及重入计数方法。`fixture_support.h` 与 `stubs/` 只提供声明、平台时间/线程查询及测试所需的依赖接口。Win32 包装在未注入故障时调用真实 event、CRT thread、TLS、wait、exit-code、handle、critical section、隐藏窗口和 timer API。测试平台设置是隔离依赖，不是完整 Blink/Oilpan 环境。

12 个注册测试保持源测试的案例 ID。`native-wait-fails` 和 `worker-self-delete` 由严格包装检查真实 `std::terminate` 的观察回执、资源尚未释放及退出码 86；没有使用 `WILL_FAIL`。MSVC worker 线程单独安装 terminate 观察器。预计终止是进程级故障边界，保留资源不是自然清理或成功关闭。包装仅能核对程序输出中的 PID；外部 Owned Job 仍需把 PID/创建时间绑定到实际子进程并收束进程树。

`native-wait-fails` 还保留第 13 次直接执行接口，可用同一严格包装检查 `GetExitCodeThread` 故障：

```powershell
cmake -DSUBJECT=<测试程序完整路径> -DCASE_ID=native-wait-fails -DSUBMODE=exit-code -P tests/native/vendor_lifetime/worker_lifetime/expect-fail-fast.cmake
```

父级 `tests/native/vendor_lifetime/CMakeLists.txt` 应加入 `add_subdirectory(worker_lifetime)`。独立构建时默认按目录层级解析仓库根；也可显式传入 `SOUI_DISTRIBUTION_ROOT`，支持整体源码搬移。配置/构建/CTest 应放在有界的 Owned Job 中执行，CTest 每案上限为 15 秒。

该回归不验证完整 Blink 生产者、GC/全局关闭、SDK/DLL、ABI、性能、长期稳定性或整个构造 OOM 恢复。动态交接只检查本次可见性，不证明所有调度的数据竞争自由；主线程案例保留真实 timer/window 行为，不认证它们的故障恢复。程序的 `qualificationPassed` 和 `constructorOomClosed` 固定为 false。2026-10-09，full-native-suite-v5 在同一 Windows 开发机的 x64 与 Win32 各通过 87/87 项注册 CTest，其中本目录 12 项公共回归均通过。CTest Owned Job 的根进程/监督器退出码为 0/0，无强制 disposal 并自然闭合；配置/编译的 0/124 仍为 genericPassed=false 的受控尾进程 partial，整体 allGenericPassed=false。该有限实测不关闭 Win7/PE、GC/OOM 或 DLL 卸载资格。
