# ADR-0123：有界输入恢复与 owner 调度

- 日期：2026-10-26。
- 状态：源码候选；未编译、未执行测试，性能采用仍待独立验证。
- 依赖：ADR-0122，基线 PR110。

## 决定

输入接纳返回 typed result，旧 `engine_input_sink::post_input(bool)` 由默认适配保留。每 Host 使用 64 普通项、16 边沿项、256 KiB 文本总量、单事件 64 KiB 的 FIFO mailbox；普通 DOM mousemove 和 wheel 不合并。Engine 另有最多 32 个共享生命周期 input service，固定大小注册表轮转，每次最多处理 8 项和 1 ms 协作预算。通知只设置 pending 和已有 wake event；不占用 ordinary/control/renewal/cleanup FIFO credit，不动态分配 continuation，不等待 UI 排空。

容量、分配或恢复状态拒绝不报告成功。首次过载封住新接纳，先处理全部已接纳输入，最后执行独立取消标记；标记不占边沿额度。恢复在 Engine owner 释放已按下键/按钮、取消未提交 composition 并清除 focus 状态。恢复期间观察实际 key/button 状态；恢复后仍按住的键/按钮被隔离到真实 release，防止重复 keydown/character 重新造成粘键。已接纳的字符、composition commit 或边沿不会为恢复腾空间而丢弃。导航/关闭的 generation 失效继续明确终止旧输入。

Engine 保留 mailbox 共享所有权；Host 关闭在 Engine owner 清除处理器并注销服务，再释放 adapter。服务回调不在注册表或 mailbox mutex 内运行，不通过消息携带裸指针。

任务调度后续保留 50 ms 老化和 future 不早跑。W00 冻结的等键契约是当前 encounter order，不能声称原实现严格 FIFO；索引实现必须明确新等键规则及对照等价范围。deadline 唤醒保持 Win7 fallback，内部使用单调时间，关闭等待异步通知退役后才释放句柄。

## 备选与回退

逐输入投递 ordinary FIFO 会让 move 洪流耗尽关键边沿额度；挪用控制/清理额度会破坏回复和关闭保障。无限排队、UI 等待、默认合并 DOM move、OOM 后遗留无 wake backlog 均不采用。按本工作包回退 mailbox/注册表及 typed 默认适配，内部对象布局改变要求匹配重编。

## 验证交接

必要用例源码覆盖容量/字节/OOM边界、已接纳 FIFO、恢复顺序、按住键隔离、IME commit、两 Host 进展、注销/停止与迟到通知。编译和运行由专人执行；当前只允许源码审查和 `git diff --check`，不据源码候选宣称性能通过。
