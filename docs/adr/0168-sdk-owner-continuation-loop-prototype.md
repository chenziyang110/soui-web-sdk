# ADR-0168：SDK owner continuation 的有限原型

- 状态：原型决策；尚未修改或默认启用公开 SDK 消息循环
- 日期：2026-11-07
- 范围：W02 持续普通输出的 owner 服务机会；不改变 Bridge 协议或权限

## 背景

preview.5 合并同一次 WebView2 drain 内的重复通知，但有剩余输出时仍投递下一条 owner 消息。普通输出长期非空时，仅减少同批通知不能证明输入及绘制已获得足够服务机会。R43 的长间隔尚未唯一归因；后续入口在输入开始前因准备时序失败，也不是 SDK 性能证据。

现有 `sdk/` 不拥有应用消息循环。SOUI 的默认 `SMessageLoop` 只有一次外部 idle，且鼠标移动、paint 等消息不重新开启 idle；仅加 idle handler 或修改 Demo 不能构成普通 SDK 接入的完整实现。替换 SOUI factory 会立即销毁旧 loop，必须早于 loop 指针、filters 和 idle 注册。Core 也不能因此强制所有 Win32 消费者链接 SOUI。

## 决定：先验证最小 SDK 调度模型

先在任务独立目录实现小型 Win32 正确性原型，封住下述具体反例，再决定公开 API、版本及正式 SDK 实现。原型不替代最终 SDK 改动，不授予吞吐、帧率或输入延迟资格，不更改已冻结 preview.5 SDK 目录。不得为该原型新增一套性能采集平台或修改原性能样本。

原型采用每 owner 线程的 message-only dispatcher。SDK 自有 root loop 取到校验过 window、token 和登记 incarnation 的私有 poke 后直接处理；绕过 root 的嵌套消息循环通过该窗口的 WndProc 使用原通知回退。不能用跨 `DispatchMessage` 的 TLS 布尔值识别 root：外部 handler 内进入 modal 时，这个值仍可能为真。

普通任务的 level-pending 与实际 queued-poke 分别记账。idle/service turn 不确认尚未被消费的 poke；仅真实匹配通知确认该责任。第一笔新普通工作仍必须唤醒阻塞循环。root 的普通 backlog 不以跨 slice self-post 链驱动：复用正常 idle 与普通 dispatch 后的有限 continuation，总机会只服务一个 adapter 的原 bounded pump。保留原最多16项/2ms协作预算，不把单个不可抢占 COM 或回调称为硬实时上限。多登记采用有界 round-robin，不能每个 Host 各花一整份总预算。

外部 idle 仍按原 SOUI idle episode 调用；普通 backlog 不重复触发业务 idle handler。生命周期、续租、清理与控制 lane 继续即时通知；本原型不改变原 lane FIFO/优先级、授权、代际、COM 前额度释放、原操作 deadline 或已执行 handler 的迟到回复处理。

## 嵌套循环与失败回退

root 消费最后 poke 后仍有 backlog，外部 handler 若此时进入 modal，WndProc 不会自行获得第一笔通知。因此进入外部 dispatch/首次外部 idle callback 前，普通 pending 必须预置一次 coalesced fallback poke；root 消费自身 poke 不再预置，否则重新形成 self-post 链。

`GetMessage`/`PeekMessage` 可在返回前派发 sent 消息，前述预置仍不足以证明全部重入边界。原型另评估仅在普通 pending 时 armed 的单 owner safety timer：root 正常 continuation 不依赖该 timer 节拍；嵌套 WndProc 首次收到 timer 后恢复 legacy repost，直至排空。不得把整个吞吐限制为每10ms处理16项。timer ID/incarnation 不复用；计数耗尽、SetTimer 或登记失败必须恢复原通知路径，不能记为已拥有 wake。取消后的旧 timer/poke 不能作用于新登记，也不能解除新通知责任。

该备选不保证任意自定义 modal、持续外部 posted 流或不返回的宿主回调有有限延迟。timer 低优先级及特殊 modal 过滤是保留风险；不能仅凭 WndProc 存在关闭漏唤醒问题。若原型不能证明明确支持范围内的进度和回退，正式 SDK 不采用这条路径。

注销先撤销服务资格，正在服务的弱生命周期/登记 incarnation 保留至调用返回；重入、异常、Run 退出和 loop 停止都恢复 legacy 通知。登记寿命与文档 generation 分离，导航不能永久取消线程登记。旧 public Host 通知与新 dispatcher 的责任不混用同一个位。

## 正式接入边界与备选

只有原型通过后才冻结正式 API。预期核心位于 SDK，SOUI factory glue 为 SDK 自有可选组件，普通模板在 App 构造后尽早默认接入；已有运行中 loop 或自定义 factory 不能静默替换。泛 Win32 应用无登记时保留原通知契约，显式使用 SDK loop 才能获得该 continuation 策略。不修改 Demo 的局部循环来宣称 SDK 已改善。

备选为保持 preview.5（局部通知减少但不关闭公平性问题），或对所有普通输出使用 timer 节拍（存在低优先级饥饿及吞吐税，未采用）。普通响应已经使用 `PostWebMessageAsJson`，不重复实施所谓 ExecuteScript 替换。

## 验证、兼容与迁移

原型只执行针对通知责任、root 不自投、实际 nested dispatch、sent 重入、timer 失败/旧消息、注销/重入/close 的最小行为集合。先读源码并保留原失败，再构建受影响小目标；不重编内核、不重复旧帧矩阵。隐藏窗口功能结果不得冒充前台输入或物理显示性能。

正式采用还需更新公开文档、受管源列表、版本和正常模板/CMake接入，完成 x86/x64、外部消费者、普通 SOUI factory 及真实 modal 回归，再将同一 SDK 变更并入既有真实负载 A/B。新 SDK 内容使用新版本，不覆盖 preview.5。回退需撤销适配器接线并恢复 legacy notification，保留已有输入/关闭/授权契约及历史失败。没有这些证据时，状态保持原型待验证，`performanceVerified=false`。

## 平台依据

Microsoft 的 [GetMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmessagew) 与 [PeekMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew) 文档确认取消息时可先派发 sent 消息，默认 posted、输入、paint、timer 的处理顺序也使普通自投链值得单独检查。该平台语义不证明本任务长间隔的唯一原因。

[SetTimer](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer) 的最小间隔为10ms，关联窗口须由调用线程拥有；原型使用窗口消息而非 TimerProc。[KillTimer](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer) 不移除已经排队的旧 timer 消息，因此撤销仍必须保留 incarnation 校验。以上来源于2026-11-07核对，不增加目标系统要求。
