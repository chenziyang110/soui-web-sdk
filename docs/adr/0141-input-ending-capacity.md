# ADR-0141：主线输入 mailbox 保留终态与排序

- 日期：2026-10-29。
- 状态：主线集成决策；当前候选未编译、未运行。
- 依赖：ADR-0123、ADR-0125。

## 决定

PR #129 集成保留主线的每 Host 有界 FIFO mailbox、普通/边沿与文本配额、Engine input service、FIFO admission ordinal、ordered cancellation、恢复隔离及 owner-applied geometry。旧冻结候选为每次按下预留 ordinary completion ticket 的 `input_admission.h` 不迁入主线；它依赖逐输入投递 Engine FIFO，会撤销主线已完成的输入洪流隔离与关闭/resize 顺序保证。

文本/terminal 输入跨 geometry 的处理继续由主线 `input_generation_applies`、实际已应用 geometry 和 trace causal-loss 契约负责，不能用 UI 的新 resize 意图覆盖 owner 状态，也不添加重复的 delivery policy。已接收边沿不会为恢复腾空间而删除；取消不伪造 keyup/mouseup/click。输入失败保留 typed 结果和唯一终态所有者。

新增资源通知故障诊断只拒绝该失败 target 的新业务输入，仍允许内部取消进入原 mailbox，并保留既有 close/drain 入口。故障必须通过 target recreation 恢复；不能把资源通知失败报告为 application ready。

## 备选与兼容性

不另建 ticket 池，不挪用 control/renewal/cleanup quota，不给终态事件插队。保留主线源测试与公共接口，避免暴露候选私有 admission 类型。回退新资源故障诊断时也不得删除主线 mailbox、ordinal、取消及 trace。

## 验证

编译与测试由专人执行。需覆盖 overload 后原 FIFO 与取消 marker 的顺序、跨 geometry 的 key/text/IME、resize/close admission fence、旧 view/document 拒绝、暂停期间 first-head publication、重入 close 与恰好一次 input terminal。源码静态审查不能证明真实输入延迟或恢复资格。
