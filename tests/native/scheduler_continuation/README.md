# 就绪任务续调度契约

独立夹具按原始字节编译完整生产 `SharedTimerWin.h`、`TaskSchedulingPolicy.h` 及 `WebThreadImpl.cpp` 的 scheduler/updateSharedTimer 方法和预算常量。生成物记录原文件和片段散列；源输入或生成脚本变化触发 CMake 重新配置。Win32 窗口、消息、时钟、任务和 heap 使用确定性替身，不创建窗口或注入宿主输入。

2026-10-26 W00 增加停止后已取出的 WM_TIMER/custom 消息、同 interval 重建新窗口、旧窗口消息与票号耗尽用例源码。当前只做源码静态审查，未编译或执行；等键策略仍保留当前 heap/vector encounter order，不能据此声明 FIFO。

覆盖批次数量/时钟预算、自补充就绪链、消息合并、当前原生输入/paint/timer/hotkey 让步、旧票号、错误窗口、重入、投递失败、未来任务、worker/关闭及恰好一次执行。接纳队列在这里是空替身，不能据此证明跨线程接纳、真实 Win32 公平性或 UI 延迟；实际接纳见 [worker fixture](../vendor_lifetime/worker_lifetime/README.md)。

最终候选尚未编译或执行。交接人员从仓库根执行以下命令；x86 将 `x64` 换成 `Win32`，并使用独立输出目录：

```powershell
cmake -S tests/native/scheduler_continuation -B out/scheduler-continuation-x64 -G 'Visual Studio 17 2022' -A 'x64,version=10.0.19041.0' -T v143
cmake --build out/scheduler-continuation-x64 --config Release
ctest --test-dir out/scheduler-continuation-x64 -C Release --output-on-failure
```

需要 Windows SDK 类型、MSVC C++17 和 Node。此入口独立于默认 vendor 注册集；它的通过不授权发布，也不能替代完整内核构建。
