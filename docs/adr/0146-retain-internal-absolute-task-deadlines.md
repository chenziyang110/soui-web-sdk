# ADR-0146：内部任务保留绝对截止时间

状态：两架构原路径指定失败已复现，六文件候选已应用，受影响源码链回归已通过；完整内核尚未重编。日期：2026-10-29。

## 背景

`WebSchedulerImpl::postTimerTaskAt()` 将调用者的WTF绝对目标D转为ceil整数毫秒，随后WebThread重新采样时间。实际重建目标含不足1ms的取整偏移和调用间隙，同一个D也可能得到不同目标。`WebPageImpl::scheduleCommit()` 同样将帧目标转为近似ceil毫秒后重投。源码能证明目标重建，尚无证据把它归为当前FPS主因。

现有TaskPair已在producer保存相对任务的createTime，owner接纳不会再重启延时。timer index、WebTimerBase与SharedTimer主路径已保存double绝对目标；原生threadpool arm只向上取整到100ns。不能重复改造P8唤醒、索引、预算或fractional frame phase。

## 决定

增加内部 `postTaskAtWithPriorityCrossThread(location, task, deadline, priority)`，通过与原相对入口共用的ownership/enqueue路径投递。TaskPair使用明确absolute标记，保留producer createTime作原观测和相对逻辑；absolute目标直接进入pending index并原样转交timer。可复用entry中已经保存的deadline，避免伪造createTime或多份可失配的目标。

owner-only `WebTimerBase::startAt(D, insertionOrder)` 设置单次timer并调用现有setNextFireTime，不换heap/index。WebScheduler的absolute adapter走此入口；原公开relative API及其取样边界保持。D始终为WTF monotonic秒，禁止直接换为原始QPC ticks或ticks/frequency。

投递仍即刻转移Task所有权，永不在post内调用Task::run。成功、OOM、退出拒绝及无效目标均恰好一次释放，锁范围与锁外析构保持。priority、50ms aging、2ms/32项预算、native-pressure、reentry、取消和原trace接线保持。等D等priority沿用当前各队列insertion order，不新增混合同/跨线程的全局提交FIFO承诺。正常调度不早于D；显式shutdown drain继续允许执行future任务。

## 输入边界

- 有限正数D，包括已过期D：原样保存。过期目标在后续异步调度机会执行。
- D≤0、NaN、负无穷：走既有零延时异步路径，不将0作为active timer目标；保留原adapter的归零行为。
- 正无穷：接纳函数消费并取消Task，在锁外释放。旧double(LLONG_MAX)后转整数存在越界，没有可靠的可沿用契约；不把无穷送入索引或整数转换。不得为此改变其他相对调用者。
- 很大的有限正数：保留绝对值，不转整数延时；沿用现有native arm上限和取消/关闭。

## Commit节拍

在scheduleCommit的producer侧只采样一次now：

```text
minimumDue = now + max(minimumDelayMs, 0) / 1000
frameDue = pacing启用且nextMainFrameTime > now
           ? min(nextMainFrameTime, now + 0.034) : now
commitDue = max(minimumDue, frameDue)
```

future目标走绝对入口，否则保留普通异步postTask。34ms仅限制frame部分，不能截短更长minimum delay。保留visibility/fresh处理、commitCount合并、CommitTask取消/析构与实际beginFrame gate；提前Commit仍可flush layout/paint，不提前beginFrame。

## 验证、兼容与回退

先在未改生产路径时，从实际WebScheduler方法进入现有真实WebThread/TaskPair/admission/WebTimerBase链路，复现小数D被重建的失败。扩展worker_lifetime源码字节投影及真实producer线程，覆盖同线程/跨线程小数D、等D顺序、late admission、异步性、过期/无效目标、ownership/OOM/退出拒绝和显式future shutdown。扩展生产CommitTask/scheduleCommit/frame gate片段，验证minimum delay、34ms上限、合并/取消和提前flush。helper数学测试不能替代该链路。

保持原字节编码与行尾，补丁、前后源SHA和所有受影响source-map/HEX门禁同步。独立review后执行相关两架构契约；现有source clock改动并入同一native批次，最终每架构一次fresh main/PCH和匹配消费者重建，实际DLL/EXE/runtime/ZIP增量另计。默认工具链、WKE/provider ABI、QoS及预算策略保持；不把函数体测试或目标精度修正称为FPS/物理输入改善。

回退内部absolute入口及两个调用点的接线，恢复相对投递，并对应重建；不撤销原P8唤醒、索引、关闭屏障或既有clock修正。

## 本次实际验证

LLVM22/MSVC142、x86/x64：原始路径各三项真实 adapter/thread/timer 精度案例均在指定 retained-D 断言失败，exit3/3且owner清理完成；Commit原片段各运行13场景，旧实现7/13通过、12断言失败、exit1/1。38个选定输入前后一致，失败原件保留。一个目标100.13225被重建为100.13339999999999，不能由此推断它是FPS主因。

应用后各架构worker34/34、Commit13/13场景及96断言通过；四个编译与四个CTest均自然0/0、owned group闭合。前者包含无效/已过期/极大D、正常异步与等D顺序、owner/producer/admission OOM、退出拒绝、取消和future shutdown。51个选定输入在运行期间不变；完整生产TU、默认MSVC143、实际系统定时精度与物理输入不在本次资格内。

首次组合结果保持passed=false：源码门禁遇到HEAD中既存的两个陈旧CMake writer pin。核对Git历史与live=HEAD后仅更新两literal；原native结果与冻结EXE未改，只重跑源码门禁，70/70通过。新macro使用独立派生类型，生产范围保留在sourceProof；unknown/重复/缺失/伪冻结与篡改均拒绝。此SOURCE报告采用authored graph，现有LLVM generated-utf8实际投影的Observer缺口仍保留，不能扩成完整编译证明。

源码补丁0027净增1629B，保留六文件其余原字节与编码/行尾；补丁反向非写入检查通过。来源与实际结果SHA见 `third_party/manifests/miniblink-absolute-deadline-20261029.json`。完整DLL/消费者/运行目录/ZIP尚未重建或计量，不作FPS提升声明；用户SOUI WebView2人工69FPS与本MiniBlink候选分开记录。
