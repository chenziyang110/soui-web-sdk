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

必要用例源码覆盖容量/字节/OOM边界、已接纳 FIFO、恢复顺序、按住键隔离、IME commit、两 Host 进展、注销/停止与迟到通知。编译和运行由专人执行；当前只允许源码审查和 `git diff --check`，不据源码候选宣称性能通过。
