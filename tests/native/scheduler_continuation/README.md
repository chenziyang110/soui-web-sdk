# 就绪任务续调度契约

独立夹具按原始字节编译完整生产 `SharedTimerWin.h`、`TaskSchedulingPolicy.h` 及 `WebThreadImpl.cpp` 的 scheduler/updateSharedTimer 方法和预算常量。生成物记录原文件和片段散列；源输入或生成脚本变化触发 CMake 重新配置。Win32 窗口、消息、时钟、任务和 heap 使用确定性替身，不创建窗口或注入宿主输入。

2026-10-29 主线集成将每次注册/注销 wait 改为两个持有的 Win7 threadpool timer，夹具保留 ready/future/idle、提前唤醒、旧票号、关闭/重建、timer 创建/SetTimer/PostMessage 失败、native pressure/reentry 和 SDK owner opportunity。W04 的 AVL 索引及等键 queue-entry order 兼容差异保留；旧 heap encounter order 不能追认 FIFO。当前只做源码静态审查，未编译或执行。

SOURCE 覆盖 pool timer 有效、native timer 失败再遇 deadline PostMessage 失败的组合：在 publication lock 外有限 SendNotifyMessage，并保留 deferred owner intent；两种消息都失败时 owner opportunity 仍可恢复，不能据此声称任意外部泵或全部 OS 唤醒资源失败时有硬实时保障。pool timer 创建失败保留 native fallback。新的 WaitForThreadpoolTimerCallbacks/CloseThreadpoolTimer 是 void API，旧 BOOL 失败返回不能伪造沿用；独立 `scheduler.deadline.close-order` 和 `scheduler.deadline.callback-drain` 检查两个 timer 在 publication lock 外完成 callback 退役后才关闭，关闭期间迟到 callback 不再投递消息。旧 expect-deadline-failure.cmake 保留为旧调用者到新契约的兼容入口。

全部 timer 安排失败会保留独立 deadlineRetry，ready 投递失败保留独立 readyRetry；回归使用实际 serviceOwnerOpportunity 重试 future-only deadline，不直接手动 updateSharedTimer。worker 的 retirement-only event 立即消费最多32项；原96任务 worker案例保留关闭前退休完成gate，防止 shutdown 排空掩盖 idle 资源滞留。普通 worker 同时采用2ms/32项预算，ready work 在批次之间使用零超时 event opportunity；显式平台 shutdown drain 保留原路径。

正常 scheduler 在 callback/observer 完全返回且 m_firingTimers 为零后，每次最多退休 32 个已完成 timer；先移除拥有记录再 deref，析构可重入且不持 provider/producer lock。多余退休项显式续调度/worker event；最后一个 Task 无下一次 idle pulse 也释放资源。关闭仍完整排空。SOURCE 包含最后一项退休与 40 项分批退休，不执行 future 任务冒充清理。

覆盖批次数量/时钟预算、自补充就绪链、消息合并、当前原生输入/paint/timer/hotkey 让步、旧票号、错误窗口、重入、投递失败、未来任务、worker/关闭及恰好一次执行。接纳队列在这里是空替身，不能据此证明跨线程接纳、真实 Win32 公平性或 UI 延迟；实际接纳见 [worker fixture](../vendor_lifetime/worker_lifetime/README.md)。

最终候选尚未编译或执行。交接人员从仓库根执行以下命令；x86 将 `x64` 换成 `Win32`，并使用独立输出目录：

```powershell
cmake -S tests/native/scheduler_continuation -B out/scheduler-continuation-x64 -G 'Visual Studio 17 2022' -A 'x64,version=10.0.19041.0' -T v143
cmake --build out/scheduler-continuation-x64 --config Release
ctest --test-dir out/scheduler-continuation-x64 -C Release --output-on-failure
```

需要 Windows SDK 类型、MSVC C++17 和 Node。此入口独立于默认 vendor 注册集；它的通过不授权发布，也不能替代完整内核构建。
