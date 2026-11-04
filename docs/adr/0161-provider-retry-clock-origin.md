# ADR-0161：帧策略 provider 退避从操作完成时起算

状态：implemented，待维护者评审；Host 接线、双架构定向构建、独立调用点复核和合成时钟回归通过。真实慢 provider 时延与宏观性能收益未测，不授予发布资格。

## 背景

ADR-0154 为 `BUSY` / `UNAVAILABLE` provider 结果规定最多三次 100/200/400ms 退避，并且只在后续已有 owner pump 机会重试。Host 在调用 `begin()` 前取得时间戳，再执行 query、必要时 apply，以及失败后的状态读回，最后把开始时间传给 `complete()`。provider 操作耗时若接近或超过当前退避间隔，截止时间会在操作返回前或刚返回时到期，使下一次已有 pump 立即重试，缩短乃至消除预期的静默间隔。

## 决定

每次退避从该次 provider 尝试完成时开始计时。Host 保留 `begin()` 的单调时钟时间，用于判断当前 attempt 是否允许启动；在 query、apply 和必要的失败读回结束后，紧邻 `complete()` 再读取 `steady_clock::now()` 并传入。无 provider 能力的失败路径和旧 setter 抛错路径也使用各自操作结束后的时间。legacy 成功路径仍由 `complete_legacy()` 处理，不变更确认语义。

状态机仍只在后续已有 owner/pump 机会检查截止时间，不增加 timer、线程、轮询或 provider 调用；每个 key 的尝试次数、最大次数、结果去重、generation 丢弃和 100/200/400ms 间隔保持 ADR-0154 既定行为。

## 兼容与回退

此修正仅调整传给既有私有状态机的单调时钟采样点，不改变公开 API、provider ABI、接口布局或重试次数。若调用者恢复使用操作开始时间，慢 provider 失败时可能立即进入下一次已有 pump 的重试；若需回退，应同时撤回本 ADR 和 Host 接线，不能更改既有间隔为任意常量来掩盖时钟起点问题。

## 验证

合成时钟回归模拟耗时 150ms 的 provider 尝试：以操作完成时刻安排 100ms 退避，完成后的 99ms 不允许重试，100ms 才允许下一次 owner 机会；再覆盖 generation 变化时旧 ticket 不得回写。该测试验证状态机时间语义。Host 源码审查确认 query/apply/失败读回都位于 `completed_at` 采样之前。它不测实际 provider 阻塞时长、显示帧率或输入到物理呈现延迟。
