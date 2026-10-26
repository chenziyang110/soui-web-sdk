# ADR-0123：有界输入恢复与 owner 调度

- 日期：2026-10-26。
- 状态：源码候选；未编译、未执行测试，性能采用仍待独立验证。
- 依赖：ADR-0122，基线 PR110。

## 决定

输入接纳返回 typed result，旧 `engine_input_sink::post_input(bool)` 由默认适配保留。每 Host 使用 64 普通项、16 边沿项、256 KiB 文本总量、单事件 64 KiB 的 FIFO mailbox；普通 DOM mousemove 和 wheel 不合并。Engine 另有最多 32 个共享生命周期 input service，固定大小注册表轮转，每次最多处理 8 项和 1 ms 协作预算。通知只设置 pending 和已有 wake event；不占用 ordinary/control/renewal/cleanup FIFO credit，不动态分配 continuation，不等待 UI 排空。

容量、分配或恢复状态拒绝不报告成功。首次过载封住新接纳，先处理全部已接纳输入，最后执行独立取消标记；标记不占边沿额度。恢复经带能力检查的 `cancelInputState` 在 Engine owner 清除原生 click/drag/touch 状态、取消未提交 composition 并清除 focus，不伪造 keyup/mouseup 或新的 user gesture。恢复期间观察实际 key/button 状态；恢复后仍按住的键/按钮被隔离到真实 release。已接纳的字符、composition commit 或边沿不会为恢复腾空间而丢弃。仅 geometry 变化不淘汰 key/character/IME，view/document 改变则明确取消旧文档输入；旧 geometry 的空间边沿封段并进入恢复。

旧 DLL 缺少取消能力或取消拒绝时，blur/composition-cancel 只算有限恢复：`input_recovery_limited=true`、causal loss，并停止该 view/document 的新接纳直到导航建立新 document。期间仍观察物理 release；不假装未知的原生按下状态已恢复，也不通过合成抬起制造 click。新 source kernel 的取消能力、无额外 click/未提交文本以及下一次正常按下仍需专人验证。

Engine 保留 mailbox 共享所有权；Host 关闭在 Engine owner 清除处理器并注销服务，再释放 adapter。服务回调不在注册表或 mailbox mutex 内运行，不通过消息携带裸指针。

任务调度后续保留 50 ms 老化和 future 不早跑。W00 冻结的等键契约是当前 encounter order，不能声称原实现严格 FIFO；索引实现必须明确新等键规则及对照等价范围。deadline 唤醒保持 Win7 fallback，内部使用单调时间，关闭等待异步通知退役后才释放句柄。

## 备选与回退

逐输入投递 ordinary FIFO 会让 move 洪流耗尽关键边沿额度；挪用控制/清理额度会破坏回复和关闭保障。无限排队、UI 等待、默认合并 DOM move、OOM 后遗留无 wake backlog 均不采用。按本工作包回退 mailbox/注册表及 typed 默认适配，内部对象布局改变要求匹配重编。

## 验证交接

输入恢复原语已实现，但新的 CANCEL_INPUT 能力在渲染 callback lifetime guard 接入前保持关闭。W07 将以同一个有界 activity pin 包住 composition cancel/blur 的完整回调栈并开放能力；不能仅靠 CheckReEnter 或 C++ try/catch 保护被原始 close 删除的 Impl。该中间 Draft 组合在缺能力时使用上述明确的有限恢复，不能称新内核的取消资格已关闭。最终系列需包括该依赖后再编译实测。

### W02 input/FIFO admission ordinal fence

The fixed Engine FIFO and each accepted input/cancellation marker share an Engine-mutex admission ordinal. The mailbox copies/preflights its payload under its own mutex first; assigning the ordinal and publishing the first atomic head/pending state are one Engine-mutex transaction. Only nonthrow fixed-slot mutation follows publication. Engine reads only atomic mailbox state while holding its mutex and releases it before mailbox callbacks, so the lock order is mailbox then Engine, never the reverse. This closes the paused-after-assignment publication gap.

An input batch stops before the current FIFO head, and FIFO cannot pop while an older published input head remains. A batch selected when FIFO was empty rechecks the FIFO head before each pop, including FIFO work appended by a callback. Resize/navigation/close therefore cannot cross older accepted edges, and newer input cannot cross an older resize. Round-robin input batches remain at most eight items/one cooperative millisecond. A FIFO task has at most 32*(80+1) previously accepted input items/markers ahead; newer admission cannot extend that predecessor set. This bounds item work, not callback wall time.

Normal ordinals never wrap and use 1..MAX-1; exhaustion rejects normal work explicitly. Recovery and existing cleanup capacity may use MAX, with recovery winning that sole tie so the marker cannot be excluded forever. Rejected input still requests ordered cancellation when the owner is running/stopping. No accepted DOM edge is deleted to make room, and FIFO lane quotas remain unchanged. Native IME negative byte counts are errors requiring cancellation; zero bytes remains a valid empty result.

Dispatch validates owner-applied context/geometry, never the UI's newer resize intent. Captured geometry becomes applied only after the actual same-view wkeResize returns; cursor publication uses that applied geometry while retaining live view/document/receipt checks. MoveWindow follows FIFO admission because it can synchronously route new-geometry input. Generic document intent is sampled only at actual main-context creation; trace base readiness remains a separate confirmed boundary.

Resize preflights its next nonwrapping geometry but publishes UI geometry only after FIFO admission succeeds. Failed admission leaves UI generation/sequence/window untouched and can be retried after capacity returns. Input sequence is monotonic across geometry changes; only document/view transitions reset it. Its allocator makes at most eight CAS attempts, never wraps, and reports zero on failure; the Host requests ordered cancellation and seals that document with limited recovery until a new document/view. Received trace identity is immutable. Cross-geometry nonspatial dispatch remains functional but has explicit native-causality loss until a schema can carry both geometries.

Regression SOURCE covers paused first-head publication, FIFO appended during a no-cutoff batch, more-than-one-batch resize/close ordering, MAX recovery/cleanup ties, rejected ordinal publication, and negative/zero/odd/oversize IMM byte lengths. These cases have not been compiled or executed.

UI independent wheel/barrier additionally requires actual native view ID, checked VIEW_CONTEXT0x40 setter and SCENE_INPUT_IDENTITY0x80. A short mutex stores owner-applied tuple; UI only try_lock, declines unknown/contended/mismatched tuple and lets the router send the unchanged packet to the ordinal mailbox. Resize/navigation/close invalidates publication until actual documentready and returned resize establish it. This tuple is not a compositor scene-current claim: matched source kernels must also compare exact active-scene view/document/geometry under B's input mutex before hit/prediction. Older DLLs fall back to Engine. Product gates do not depend on recorder/detail/QPC availability. SOURCE covers contended publication and pending resize/document→wheel FIFO fallback; active compositor scene proof belongs to B's exact source gate.

### W03 deadline 实现

主线程不再安装固定 16 ms 周期 tick。ready 合并窗口续调度；future 只保留最早绝对单调 deadline；idle 取消 native timer 和注册 wait。waitable timer 动态探测高精度创建标志，失败回到 Win7 auto-reset timer；不调用永久 `timeBeginPeriod`。相对截止采用向上取整，owner 收到消息重查单调时间，提前消息重新安排而不执行 future。

每次安排含不回绕票号，thread-pool wait 只投递静态窗口和票号。取消先失效票号，再在 publication lock 外 `UnregisterWaitEx(INVALID_HANDLE_VALUE)` 等待回调退役，之后才能取消/复用/关闭 timer HANDLE。native WM_TIMER 使用同票号作为低优先级故障兜底；ready 遇原生压力/重入暂停生产者补投，临时 10 ms fallback 仅为未解决工作存在。SDK 经尺寸/版本/能力验证的可选 WKE 表，在每个有界 native batch 后给 owner 一次调度机会；旧嵌入者保留低优先级消息泵约束，不能声称任意外部消息泵的硬实时上限。跨线程 posted 队列耗尽时可用不等待 owner 的 SendNotifyMessage；同线程保持异步 post 语义。

原 tick 隐含调用的 observer、临时 V8 对象清理和取消 timer 释放仍在真实 scheduler turn/关闭中执行；不为无任务 idle 人造这些周期回调。SDK cold hook 在 provider finalization 前撤销，旧 DLL 无能力时保留 fallback。

Deadline callback 的 PostMessage 失败会先发布 deferred owner intent，再在 publication lock 外有限 SendNotifyMessage；不假定 native SetTimer 一定成功，也不在 wait callback 中取消/关闭自己的注册。CloseHandle 失败保留原 timer 引用并非继续失败；UnregisterWaitEx 失败不能继续复用/关闭尚可能被等待的 timer。所有 OS 唤醒资源同时失败时仅有保留 intent，任意外部嵌入泵的时限仍不作保证。

正常 scheduler 在 callback/observer 返回、m_firingTimers 降为零后最多退休 32 个 completed timer。退休先从拥有 vector 移除再 deref，析构不持任何 provider/producer lock，重入由独立 retirement guard 抑制；余项显式续调度/worker event。开始下一 turn 的退休也有同一界限；shutdown 完整排空。最后一项 Task 的析构不再依赖另一个 idle pulse，支持 close receipt 在真实资源释放后闭合。

官方契约参考：[SetTimer 最小 10 ms](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer)、[SendNotifyMessage 跨线程不等待](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendnotifymessagew)。构建和时钟/睡眠/失败/Win7 兼容仍未实测。

### W04 索引和接纳

保留既有拥有 timer 的 deadline heap，在它旁边建立非拥有索引：priority AVL 每节点包含该 priority 的 deadline heap，并缓存子树最小 deadline。最高 priority 的就绪候选通过子树摘要直接查询；aged 候选取全局最早 deadline，50 ms 老化是查询条件，比较器不随时钟改变。不做全量 future 扫描或先完成整个 promotion 批次才能执行的屏障；少量高优先级 ready 不会被大量未到期 future 的维护挡住。任意整数 priority 仍受支持。

exact equal priority/deadline 的身份顺序从旧 heap encounter order 改为不回绕的 uint64 queue-entry order，这是明确兼容差异；旧实现不能追认 FIFO，对照回归只对策略键要求相同，等键身份单独验证新规则。

producer 待接纳任务有相同索引，mutex 内一次只选择/移除一个，拥有 vector 用 swap-last 移除避免 erase 搬移。每四次接纳保留一次 oldest-deadline 份额，其余优先已到期/老化工作；普通 main turn 仍最多 32 项并与 2 ms 截止共享，worker 接纳也按 32 项/50 ms 有界。关闭拒绝新 producer 后允许有限既有集合排空。所有任务和析构仍在 producer lock 外转移/执行，取消索引随同原所有权恰好移除一次。

索引每个活动 Timer/TaskPair 增加一个固定 Entry 和一个非拥有指针；priority 节点数量不超过不同活动 priority 数加四个标准空节点。四个标准 bucket 保留向量高水容量以避免每帧重新分配，其他空节点立即释放。`statistics()` 报告活动项、节点和保留结构字节（不含 allocator 元数据/不透明 Task 载荷）。节点/向量容量在 timer 状态变化前准备，失败不破坏原活动索引。原第三方 void post 的不透明 Task 载荷与全局 admission 项数仍没有统一新容量协议；本包没有为性能新增静默删除/进程 fail-fast，也不声称这个既有限制已关闭。后续 typed tryPost/逐调用方失败策略必须另行覆盖全部关键 caller。

必要用例源码覆盖容量/字节/OOM边界、已接纳 FIFO、恢复顺序、按住键隔离、IME commit、两 Host 进展、注销/停止与迟到通知。编译和运行由专人执行；当前只允许源码审查和 `git diff --check`，不据源码候选宣称性能通过。

### W05/W07 optional Host policy adoption

Normal fractional CPU pacing requires matched cap0x10; older kernels retain their legacy integer/experimental policy. The normal Host sets mirrorRequired=false once after DIRECT_HWND presentation succeeds, only through version/field-boundary/cap0x4/non-null/OK0 checks. Failed adoption restores explicit mirrorRequired=true. Benchmark/DC consumers retain the mirror; B's sticky DC/transparent/layered/nonindependent rules override false. No per-frame mirror toggle or new old-DLL requirement is introduced.

Native visibility uses optional cap0x100/setHostVisible with version/field-boundary/pointer/OK0 checks. UI show/hide and each existing UI pump sample IsWindowVisible(child) plus !IsIconic(root owner). A fixed latest bool/dirty bit feeds the existing one-pending Engine pump; no new queue/timer is created, failed publication stays unknown/pending for the next opportunity, and unmount cannot queue more pump captures. Stable state avoids repeated calls; observed hide/show retains dirty even when final bool matches, so B true publication always resets phases and requests full fresh work. No DOM input or JS visibility event is synthesized/coalesced. SDK acknowledgement means actual setter OK only, never desktop present or runtime-exit completion; old DLL keeps native-window fallback.

### W01 接入与 W09 宿主成本

SDK 与 vendor 各自保留 POD 当前因果上下文：TaskPair/Timer 和固定 Engine FIFO 保存同一 input range/generation、非零 task ID、parent ID 和真实 QPC；post/begin/end/cancel 明确分开。实际引擎 dispatch 完成 generation 检查后设置 vendor view context，返回时 RAII 清除；T3b/visible 标志不伪造。T1 的 SendInput 标记仅写 related_id，不作身份/授权依据。已接纳输入关闭/异常终态显式记录；UI 使用共享 thread scope，最后 Host 退役才 detach，worker 在真实 loop 退出后 detach。

实际 wkeGetWebviewId 的正 ID 在冷 owner 创建后、UI 输入可用前发布；不可用则 unknown/causal loss，不用指针充当 view ID。非输入 base identity 仅在实际 documentready 与已返回的同 view wkeResize 两个条件成立后经 cap0x40 setter 发布；导航意图不发布。输入上下文经 cap0x8/field boundary/OK0 验证，返回时清除；跨 geometry 的非空间输入保留收到时的身份与功能投递，但不伪造 native frame range，明确 causal loss。

内部恢复标记 sequence=0，不为原 physical input 再签第二个 terminal；原 rejection/stale/exception/close 各有唯一终态所有者。IME 查询/拷贝失败为已经创建的 T1 签一次 rejection，再请求匿名恢复，不生成虚假的第二次 T1。回归 SOURCE 通过实际 recorder/mailbox/router 对原 copy rejection、handler exception、queued close 和显式 cancel 统计恰好一个 terminal。worker 严格 closure 追加实际 vendor InputFrameTrace.h/cpp（16 live sources、15 source copies），未用 trace stub 替代。

Handler 在构造时冷分配共享拥有对象，服务在 mutex 内取得 shared pin、锁外调用；重入 close 只释放 mailbox 自身引用，不能析构仍在调用的 capture。取出 recovery marker 时把 terminal 身份转交局部 owner，close 不再签第二份终态；callback 返回后若已关闭，不重写 quarantine/pending。SOURCE 增加 explicit cancel→handler 重入 close 的唯一终态/捕获对象寿命，以及延迟 P1 wake→空机会确认→P2 实际 Engine 输入进展。

Bridge 逐事件开文件的实验 logger 改为数值阶段事件，去掉调用处日志专用字符串格式化；旧 `SOUI_BRIDGE_TRACE` 不再创建文件。细节通过统一 recorder/exporter 开关导出。每次 pending 接纳仍通知原 Engine auto-reset event，由系统 event 合并；自定义 wake_pending 合并未采用，延迟 producer 不能毒化后续 wake。拥有 packet 在 owner 借用 const reference，避免 dispatch 的第二次文本复制；不改变 DOM 事件数量。

新增 native/provider/input/FIFO/serialization 成本直方图。`SOUI_ENGINE_SHARED_TURN_BUDGET_US=100..5000` 是默认关闭的协作总预算候选：native/FIFO 截止取组件与总剩余的较早值；provider/input/FIFO 每轮至少保留一次有界服务以保证进展。单次 provider/输入批次或用户回调仍可超时，候选不能冒充抢占。没有 profile 时不默认采用、不凭常数推算收益；跨 MiniBlink provider 批次的更细剩余预算协议仍待数据决定。

### 最终关闭 fence

`BlinkPlatformImpl::closeThread()` 保留原 registry 数量/指针检查和真实 join 流程，失败诊断后 `std::terminate()`，调试器继续也不能到达 `delete this` 或成功 finalize。worker fixture 保留实际生产 fence，`concurrent-registry-close` 改为预期非继续失败，新增 `registry-live-fence`；严格验证尚未释放的原 worker/scheduler/event/registry。最终 wkeFinalize 的完成 guard 由集成包配套，不能在 fence 失败后签发完成收据。
