# Worker 生命周期真实源码回归

## 绝对截止时间 GREEN 候选（2026-10-29）

当前保留原 23 项与上述 RED 准备的 3 项精度回归，另增 8 项边界回归，共 34 项。此状态仅表示候选已准备；两架构 RED 实测由父任务保存，本目录未自行配置、构建或执行 GREEN。

`scheduler-absolute-inputs-same-thread` / `scheduler-absolute-inputs-cross-thread` 将 past、等于 now、0、负数、负无穷、NaN、正无穷逐项同时送入真实 adapter 与内部 absolute 入口，核对前六类异步执行、正无穷消费并取消、active timer 不使用 0，以及恰好一次析构。`scheduler-absolute-huge-cancel` 保留超出整数毫秒范围的有限 D，确认真实 native arm 已调用、正常 fire 不提前，再通过真实 timer `stop()` 与 owner retirement 取消。

`scheduler-absolute-oom-owner` 覆盖 reserve / timer / timer index 分配失败；`scheduler-absolute-oom-producer` 覆盖 TaskPair / pending index node / node vector / pending vector，且核对失败后真实队列可继续接纳；`scheduler-absolute-oom-admission` 覆盖跨线程 TaskPair 接纳转交后的 owner 分配失败。分配故障只扩展原全局 new 的受控 seam，Task 先分配再武装故障。Enter/Leave 包装只观察实际源锁深度，任务析构必须在其外。`scheduler-absolute-exit-rejection` 与 `scheduler-absolute-future-shutdown` 使用真实 worker/native join，分别核对已退出拒绝及 shutdown 允许 future Task 在 D 前执行的既有契约。所有 receipt 均存活到真实 owner 完成销毁。

## 绝对截止时间 RED 准备（2026-10-29）

保留原 23 项，新增 `scheduler-absolute-same-thread`、`scheduler-absolute-cross-thread`、`scheduler-absolute-late-admission`。从当前真实 `WebSchedulerImpl::postTimerTaskAt` 精确字节片段进入真实 `WebThreadImpl` / pending index / admission / `WebTimerBase`；仅增加依赖声明，不手写 adapter。当前固定 16 个 live inputs、16 个 exact copies 和 22 个 payloads，保留完整源文件 SHA 与逐字节 HEX 门禁。

三个案例均投递相同小数目标 D=100.00025，并在两次 post 之间推进 producer clock。跨线程案例使用真实 CRT 原生 producer；late admission 在 D 之后、旧 ceil 目标之前才交给 owner 接纳。通过实际 timer 的 `nextFireInterval()` 检查保留 D（过期目标余量应为零），再检查异步、正常调度不提前、到期执行、等 D 顺序及任务恰好一次析构。RED 预期原因是 `absolute-deadline-retained-D-assertion-failed`，不是新 API 缺失；收尾先销毁真实 owner，使其 future shutdown drain 完成后才释放任务 receipt。

本轮仅准备，未配置、构建或执行。后续采用 ADR-0146 生产变更时，必须先人工审查六文件 delta，逐项核对原始字节与本映射当前 SHA；本夹具固定其中 WebThreadImpl.h/.cpp、WebTimerBase.h/.cpp、WebSchedulerImpl.cpp，WebPageImpl.cpp 由其专属夹具绑定。只对已审查文件更新 whole-source pin 与 whole-file copy 的长度/SHA；scheduler adapter 必须重新定位完整函数区间并计算精确片段 SHA，ctor/dtor 片段原 SHA 应保持。其他 source pins/片段只允许确认未变，不能批量接纳未知 producer 字节；payload pin 仅更新本目录实际改动文件。

## 主线性能集成（2026-10-29）

当前保留 23 项注册及生命周期/trace 来源，`worker-scheduler-no-count-cap` 改为 `worker-scheduler-task-budget`：正常 worker admission 与 callback 共用 2ms/32 项合作预算，已就绪工作批次之间不额外等待 1ms；显式关闭仍为原有 50ms drain 且没有 32 项上限。主线程 owner 让步、FIFO、AVL 和原失败包装保留。当前映射固定 16 个 live inputs、15 个 exact copies 和 22 个 payloads；下文旧数量及实际结果仅对应各自日期，不继承为当前通过。本轮未执行构建或测试。

## 就绪任务续调度候选（2026-10-26）

本轮在原 18 项上新增 4 项：`main-scheduler-continuation`、`main-scheduler-first-task-wake`、`main-scheduler-admission-budget` 和 `main-scheduler-admission-shutdown`。只在夹具中停用周期 timer，以真实窗口消息分发验证首任务唤醒、积压链完成、跨线程接纳预算及保留批次关闭；核对未来任务不提前和恰好一次清理。首任务案例还覆盖长回调后提交的正延时从实际 post 计时。源码映射更新三个完整 vendor 文件及夹具输入，原有关闭片段和其他输入保持原字节。最终候选按用户要求未继续编译或执行，交由后续人员验证；这些案例也不证明真实硬件输入公平性。

## 当前源码绑定修复（2026-10-13）

0.5 Native 源码采用后，本目录的 current 输入仍绑定采用前 5 个文件及 3 个旧字节偏移，导致正常配置被 byte gate 拒绝。更新时逐项验证完整文件，并以原片段 SHA 定位相同方法；registry/TLS 的三个片段原字节不变，仅位置移动。完整 WebThreadImpl 则包含已经采用的 retirement/witness 方法，测试依赖投影从真实 WebThread.h 额外提取该接口声明，不能手写另一套关闭实现。新增第 12 个 exact fragment，严格 copies 数同步为 12；13 个 current 源输入与所有 payload/HEX 前后校验仍保留。

此次只修测试侧 current 来源和必要声明，原 12 个行为断言、故障包装、时间预算及下文历史实测原件保持。其他 vendor 子目录分别核对 current 与派生片段，不能把全文件 hash 更新替代 fragment 对齐。当前只有文件/Node 数据检查，未执行 CMake、编译或 Native 回归，也不继承旧 2026-10-09 结果。

## 调度让步与关闭范围（2026-10-21）

当前共 18 个 CTest，包含原 12 项生命周期回归和新增 6 项调度回归：主线程任务数量预算、主线程时钟预算、worker 无数量上限、worker 原时钟预算、最终关闭清空，以及退出标志尚未设置时的平台关闭清空。新增案例不继承下文历史实测结果。

主线程预算案例保留真实隐藏窗口与 `WM_TIMER` 分发，由任务投递独立原生消息，验证任务积压时消息泵得到执行机会。依赖时钟只在新增案例中受控，原 12 项仍使用真实时钟。案例同时验证已保留任务最终逐一执行和未来任务不提前执行；worker 案例观察真实批次之间的 event wait。最终关闭案例保留未来任务的原有清空契约。

`shutdownProof` 额外固定并生成实际 `BlinkPlatformImpl::shutdown` 函数的完整字节区间。证明门要求其三个 `currentThread` fire 调用均在 `m_mainThread->shutdown()` 前、且使用同一真实方法，再从这些调用派生测试入口标识。关闭范围案例调用该真实入口执行超过 32 项就绪工作，并在返回后继续接收新任务，防止以提前设置退出标志来绕开测试。它不编译或手写另一套平台清理算法。

本目录提供一个 Windows/MSVC C++17 测试程序和 18 个 CTest。它从当前 Miniblink 源码生成输入，覆盖 worker 构造、启动交接、真实线程退出、注册撤销及主线程计时器行为。测试目录不保存历史 before 快照，也不包含开发机绝对路径。

`source-map.json` 固定 13 个当前源码输入的完整 SHA-256/字节数，以及 12 个生成输入的精确字节区间。完整文件按原字节复制；片段通过 HEX 解码写入，不做变量、美元符号、生成表达式或换行替换。片段必须为 NUL-free、LF-only 且保留末尾 LF。配置时和编译前都验证完整源码，随后逐个比较生成内容与真实源码区间；改变源码必须重新审查和更新映射，不能改成手写生命周期算法。

程序编译真实 `WebThreadImpl`、`WebTimerBase`、`SharedTimerWin` 和平台注册/TLS、scheduler 构造析构及重入计数方法。`fixture_support.h` 与 `stubs/` 只提供声明、平台时间/线程查询及测试所需的依赖接口。Win32 包装在未注入故障时调用真实 event、CRT thread、TLS、wait、exit-code、handle、critical section、隐藏窗口和 timer API。测试平台设置是隔离依赖，不是完整 Blink/Oilpan 环境。

原 12 个注册测试保持源测试的案例 ID，新增 6 项使用独立调度案例 ID。`native-wait-fails` 和 `worker-self-delete` 由严格包装检查真实 `std::terminate` 的观察回执、资源尚未释放及退出码 86；没有使用 `WILL_FAIL`。MSVC worker 线程单独安装 terminate 观察器。预计终止是进程级故障边界，保留资源不是自然清理或成功关闭。包装仅能核对程序输出中的 PID；外部 Owned Job 仍需把 PID/创建时间绑定到实际子进程并收束进程树。

`native-wait-fails` 还保留一个未另行注册的直接执行接口，可用同一严格包装检查 `GetExitCodeThread` 故障：

```powershell
cmake -DSUBJECT=<测试程序完整路径> -DCASE_ID=native-wait-fails -DSUBMODE=exit-code -P tests/native/vendor_lifetime/worker_lifetime/expect-fail-fast.cmake
```

父级 `tests/native/vendor_lifetime/CMakeLists.txt` 应加入 `add_subdirectory(worker_lifetime)`。独立构建时默认按目录层级解析仓库根；也可显式传入 `SOUI_DISTRIBUTION_ROOT`，支持整体源码搬移。配置/构建/CTest 应放在有界的 Owned Job 中执行，CTest 每案上限为 15 秒。

该回归不验证完整 Blink 生产者、GC/全局关闭、SDK/DLL、ABI、性能、长期稳定性或整个构造 OOM 恢复。动态交接只检查本次可见性，不证明所有调度的数据竞争自由；主线程案例保留真实 timer/window 行为，不认证它们的故障恢复。程序的 `qualificationPassed` 和 `constructorOomClosed` 固定为 false。2026-10-09，full-native-suite-v5 在同一 Windows 开发机的 x64 与 Win32 各通过 87/87 项注册 CTest，其中本目录 12 项公共回归均通过。CTest Owned Job 的根进程/监督器退出码为 0/0，无强制 disposal 并自然闭合；配置/编译的 0/124 仍为 genericPassed=false 的受控尾进程 partial，整体 allGenericPassed=false。该有限实测不关闭 Win7/PE、GC/OOM 或 DLL 卸载资格。
