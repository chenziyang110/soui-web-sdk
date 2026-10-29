# MiniBlink 调度与输入主线集成交接

本候选基于当前 `main` 保留 ADR-0123/0125 的 mailbox、Engine ordinal、任务 AVL 索引、input/frame trace、应用 geometry 与取消恢复。旧候选逐事件 completion tickets、全 heap 剪枝及独立 producer batch 已由主线实现覆盖，未覆盖写回。当前决定见 [ADR-0143](../adr/0143-miniblink-deadline-scheduler.md) 与 [ADR-0141](../adr/0141-input-ending-capacity.md)。

增量为持有的 Win7 deadline/retry threadpool timers、独立 ready/future retry intent、短 deadline 期间平衡 timer precision、worker 正常 turn 的2ms/32 callbacks 与 ready 零等待机会。关闭仍先封 publication、锁外退役 callback、最后销毁窗口；显式 platform drain 与原生命周期失败边界保留。

SDK 沿用主线 fractional mainFrameRate；只有未采用该策略、CPU、未显式覆盖 interval 且 kernel 宣告 interaction precise capability 时才采用 drawFrameRate fallback。普通 DIRECT_HWND Host 通过可选 on-demand flag 与原 mirror consumer setter 减少镜像需求，benchmark/DC/透明等 kernel sticky 条件保留。资源通知状态按 capability 每250ms检查，notification-failed 锁存 E_RESOURCE_WAKE、applicationReady=false，允许取消和原关闭排空，要求重建 target。

当前只做静态源审查与 diff 检查，未编译、未执行测试、未采样，也未证明零体积增长或 Electron 性能对齐。专人需执行 x86/x64 匹配 kernel/SDK 构建、`kernel.native.worker-lifetime.*`、`tests/native/scheduler_continuation` 独立 CTest（含 close-order/callback-drain）及原输入 FIFO/恢复/trace 回归；同时核查 sleep/resume、Win7 timer、失败重投、两 Host 公平性、资源故障重建与真实输入时延。完整 command/负载及体积预算见 [验证交接](validation-handoff.md)。
