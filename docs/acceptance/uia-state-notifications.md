# 有限原生 UIA 状态通知

windows_uia_provider::publish 接受可信 owner 的公开投影后唤醒原 owner。现有 pump 每次最多派发八条业务命令、调用十六次 UIA 事件 API；WndProc 继续转交已有 wake，普通消息泵也可调用 pump。publish 的 S_OK 表示投影已接受，不证明任何客户端已消费事件。

当前通知覆盖公开名称、可用/屏外状态、边界、任务状态、非敏感输入值、单选标志、焦点，以及节点结构/身份/次序变更。未开始的相邻状态可合并；已经开始的批次先结束，再处理最新状态。结构变化发送 ChildrenInvalidated，消费者应重读子树。secret 不允许投影值或状态，不提供 Value pattern，也不发送 Value 事件。

通知在共享锁外调用 Windows API，保留实际 HWND/API 借用并禁止递归 pump。close 立即撤销，待真实借用释放后才允许销毁窗口与 controller。尚未排出的通知被撤销。系统 API 失败由 pump 返回，已接受状态不回滚；可由后续 owner pump 重试，不可把失败伪装为已送达。

新增原生 CTest：sdk.native.uia.property-events、tree-events、revoked-events。与八个原用例一样，每例一个进程；COM 初始化负例不可合并到已经激活 UIA 的进程。独立提案两架构各 11 项自然通过，详见 ADR-0109，尚未采用为当前候选证据。测试使用真实 UIAutomationCore 客户端；不等于完整屏幕阅读器或目标系统认证。
