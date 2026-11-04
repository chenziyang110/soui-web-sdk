# ADR-0153：输入 mailbox 按首次待处理发布通知 Engine

- 日期：2026-11-02
- 状态：私有实现与两架构 standalone 回归完成；实际 Host 集成和性能待验
- 范围：MiniBlink Host 私有输入 mailbox 与 Engine 通知调用；不改公共输入协议

## 背景

`post_input_typed` 在每次提交后，只要 mailbox 仍 pending 就通知 Engine。
auto-reset event 能合并信号状态，却不会免除每次通知的 Engine 锁和 `SetEvent`。
输入接纳已经使用有界 mailbox、全局 ordinal、FIFO cutoff 和有序 recovery；
这些正确性机制不需要重新设计。当前没有数据证明重复通知是端到端延迟主因。

## 决定

增加私有 `input_mailbox::post_with_notification`，返回原接纳结果与
`wake_required`。既有 `post` 保留为返回原枚举的兼容包装。

在同一次 mailbox 持锁事务中读取此前 pending，运行完整的既有接纳/恢复逻辑，
然后计算 `wake_required = !was_pending && pending_after`。拒绝后首次发布的
recovery 也能产生 true，不能只通知 accepted 结果。载荷复制、Engine 锁下的
ordinal/head/pending 发布、失败载荷在锁外退休均保持原顺序。
Host 在持有 mailbox 生命周期引用时，于 mailbox 锁外按 receipt 通知 Engine。
不增加永久 armed 标志，不合并输入，不改容量、代际、边沿、IME、终态或关闭顺序。

## 进展与交错

- empty → pending：本次 producer 的 receipt 负责通知。
- pending → pending：不重复通知；Engine 等待条件已有 pending 检查，继续服务。
- consumer 发布 empty 后的新 producer：重新得到 true，独立负责新通知。
- producer 迟到通知时数据已由其他机会排空：允许一次无害的多余唤醒；它不设置
  任何阻止后续 producer 通知的状态。
- deadline 中途让出：未消费的 pending 保留；Engine 不等待新 producer 才前进。
- recovery 首次发布与普通事件一视同仁；accepted 事件先排空，恢复仍服从 ordinal。

consumer 的 empty 发布和 producer 事务都在 mailbox 锁下串行。
Engine 检查 pending 时不获取 mailbox 锁；新发布仍在 Engine commit 锁事务中可见。
不引入 Engine → mailbox 的锁顺序。关闭后迟到通知只调用既有 Engine 生命周期守卫，
不能重新打开 mailbox 或执行已关闭回调。

## 备选与兼容

保留逐包通知是低风险回退；单独维护 armed 位容易被迟到旧通知与排空交错污染，
本次不采用。锁自由队列、输入优先级、pointer move 合并与共享预算默认变更均不在范围。
没有公共头、DLL ABI、页面 API、最低 Windows 或工具链改变。旧 `post` 调用方继续
负责自己的通知；生产 Host 接线由集成者统一修改。

## 验证与交付

扩展实际 Engine 用例：迟到旧通知→排空→新包、最后一次 pop 后的新发布、拒绝但首次
recovery、一次通知后的多轮 deadline 让出，以及 close/unregister。源级用例覆盖
accepted、capacity、no_memory、stopped、recovering 与 rejected-publication 结果。
保留原有 FIFO、IME、终态、32 Host、共享截止与 provider 回归。

有界 burst 对照记录通知次数及所有接纳事件实际完成；该计数只能证明减少通知，
不能代替 CPU、FPS 或输入到反馈的实测。正式性能采样 trace-off，真实 Host/provider、
焦点、IME 与窗口测试由总控在同一最终载荷验证；未执行项保持待验。
回退仅恢复 Host 逐 pending 包通知，不撤销输入恢复和终态安全机制。

## 2026-11-02 源码回归记录

基于 `a4fc394427c99a3331abfb2e3e3307ae27503dd0` 加本次 I01 修改，
实际执行 `tests/native/input_admission/standalone` 的 x64、x86 Release 构建，
使用 MSVC v143（19.44.35226.0）、Windows SDK 10.0.19041.0、C++17。
两架构各自 `provider.actual-engine-clock-abi` 1/1 通过，包含既有夹具和上述新增用例。
命令、日志、实际测试 EXE 与源码 SHA-256 由本次外置 I01 收据绑定，交总控汇总；
没有把未编译的实际 Host/provider DLL、GUI、CPU/FPS 或输入到反馈性能算作通过。
首次 configure 未固定 generator 的 SDK 版本，曾选择 26100；保留该配置日志，
未使用其编译，随后在新目录显式固定 platform SDK 19041 后完成上述两架构验证。
