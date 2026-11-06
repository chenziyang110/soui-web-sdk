# ADR-0143：主线调度器上的持有计时器与协作续调度

- 日期：2026-10-29。
- 状态：PR #129 源码候选；编译、运行、性能与 Win7 资格交专人验证。
- 依赖：ADR-0123、ADR-0125；保留主线输入和任务索引契约。

## 背景与决定

主线已经具备单调 deadline、主线程 2ms/32 项协作预算、owner opportunity、50ms 老化、priority AVL/deadline 索引、输入 mailbox 与 FIFO ordinal。旧性能候选的全 heap 扫描剪枝、另存接纳 batch 和逐输入 ordinary-ticket 方案不能覆盖这些主线实现。

在主线算法上，仅将逐 deadline 注册/注销 wait 改为两个持有的 Win7 threadpool timer：一个唤醒 future deadline，一个在消息投递失败时重试 publication。正常 ready 消息合并；readyRetry 与 future deadlineRetry 独立，未来 deadline 不得阻挡 ready 重投。原生压力/重入时保留 owner opportunity 与一次性恢复安排；保留跨线程 SendNotifyMessage fallback。窗口只携带静态状态及不回绕票号，owner 重查单调时钟，旧消息不能提前执行 future。WM_QUIT 留给外层消息泵。

取消只停用 deadline timer 并失效票号，不在每次重臂同步等待 callback。关闭先在 publication lock 下关闭发布，再在锁外取消、等待两个 timer callback 退役、关闭 timer，最后销毁窗口；callback 只发布消息、不调用 Blink。已排队的旧 callback 可以发布当前 ticket，owner 的 deadline 检查保证不提前执行。

不超过50ms的 future deadline 临时申请 1ms timer precision，覆盖30–1000Hz活动帧的截止；ready、idle、取消与关闭归还申请，不永久持有。申请不能保证实际 Windows 调度精度或帧率。threadpool timer 创建失败保留 ticketed native SetTimer；所有安排/投递均失败时保留 owner intent，不将其声称为任意外部泵的硬实时保证。

正常 worker turn 同样使用 2ms/32 callbacks，ready backlog 采用零超时 event opportunity，不再在每批 ready work 间强制等待至少1ms。生产者 AVL 索引、真实追踪上下文、32项有界 retirement 和 shutdown 的显式完整 drain 保留。运行中的 JS、GC、COM 或用户 callback 不能被该协作预算抢占。

## 兼容性、备选与回退

不改变公共 WKE 表布局、任务 priority、50ms aging 或等键 queue-entry order。内部布局和 SDK/kernel 必须匹配重编。不采用每帧 polling、另造 ready FIFO、静默丢弃任务或延长退出期限。可按本 ADR 的 timer/worker delta 回退到主线已有实现，不能回退主线输入、索引、trace 或生命周期修复。

## 验证交接

未编译或执行。`tests/native/scheduler_continuation` 绑定生产字节，保留 ready/future/idle、提前消息、旧 ticket、native pressure、重入、创建/投递失败及 owner 恢复的源码用例。旧 wait 的 BOOL 失败接口已被 void threadpool 退役 API 替代，专门入口改为 `scheduler.deadline.close-order` 与 `scheduler.deadline.callback-drain`，检查锁外等待、两个 callback 退役后才关闭，以及关闭后的迟到 callback 不发布。真实 Win32 worker fixture 增加 `worker-scheduler-task-budget` 并保留其 clock-budget 与所有生命周期/关闭案例。上述用例源码不等同于执行结果。
