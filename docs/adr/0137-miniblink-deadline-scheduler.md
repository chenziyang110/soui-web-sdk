# ADR 0137：MiniBlink owner 截止时间调度与有界搬运

状态：实现候选，待集中编译与运行验证。日期：2026-10-29。

## 背景

主线程 shared timer 固定每 16ms 调度，单次 timer callback 预算 50ms；跨线程投递会持续搬运到生产者队列为空。SDK 外层 2ms 批次不能抢占该内核 callback。已有工作线程 deadline 等待、timer heap、50ms 老化及关闭排空保留。

## 决定

- 内核调度在单个任务边界按 2ms/32 项让步；不能抢占 JS、GC 或任意回调。
- 跨线程发布保留 producer vector，owner 每次仅消费有限项，未消费批次由 owner 持有，不反复拷回 producer。停止时完整排空所有已接纳工作。
- main owner 立即工作使用带票号的合并消息；未来任务按最近 deadline 重设 Win7 原生 threadpool timer，period 为 0，回调只发布 owner 消息。空闲撤销定时，不新建帧线程。回调消息仍由 owner 检查实际 deadline，提前或旧唤醒不提前执行任务。
- 原生 input/paint 等等待时用一次低优先级 WM_TIMER 让步，抑制 producer 不断补发 posted message；嵌套 CheckReEnter 阻止执行时也保留一次恢复机会。正常不维持周期 timer。
- future timer 对象创建失败时仅在 owner 回退一次性 SetTimer；亚 16ms future 工作使用已有 winmm 的局部精度请求，空闲/长 deadline/关闭配对释放。跨线程 PostMessage 失败通过独立、平时未 arm 的 threadpool timer 在 8ms 后重试，绝不从 producer 对 owner HWND 调 SetTimer。fallback WM_TIMER 使用不重置的递增票号；取消/重 arm 同时解除 deferred，旧已入队消息不能消费新状态。WM_QUIT 等待时停止自补 ready 消息；恢复广播会重新核对单调 deadline。
- threadpool callback 只持短发布锁；stop 先封闭发布，再在锁外 cancel、WaitForThreadpoolTimerCallbacks(TRUE)、Close 两个 timer，最后销毁 HWND。没有 callback 等 owner 或执行页面代码。不能用已经过期、period=0 的 timer queue 对象 ChangeTimerQueueTimer 代替可重 arm 的 threadpool timer。
- 调度时间使用 WTF 单调时钟，不改变 JS Date、网络或业务墙钟。
- ready 选择按现有 deadline heap 剪去未到期子树，保留原优先级、50ms 老化及同键原 heap 顺序；不引入第二套有失效风险的 ready 索引。

## 兼容和边界

不改变公开 WebThread ABI、输入 FIFO、reply/cleanup 预留用途或 public API。私有 WebThreadImpl 布局变化要求匹配重编。调度恢复不进入暂停业务 FIFO，不在任意 JS 栈重入。最后输入额度耗尽的跨 Host 恢复协议仍需 Host 明确处理；本补丁不声称仅靠缩短调度即可避免所有接纳失败。

不新增运行 DLL/下载；新增代码仍须通过最终相同功能与架构下的压缩包、运行文件、内核运行载荷体积门禁。

## 验证和回退

增加 scheduler policy oracle/未来子树剪枝用例；真实 worker fixture 覆盖无周期 timer 的 main ready/deadline、deferred 取消/重 arm、旧 timer 票号、WM_QUIT 和关闭。窗口重建/HWND 复用、失败重试、持续压力仍待集中验证。保留 failure injection/来源映射身份要求。由专人集中构建、生命周期、输入/IME、DevTools 暂停/恢复及体积/性能验证；本 ADR 不构成通过证据。回退该调度补丁并重编匹配内核，不混用私有布局。

API 根据 Microsoft [SetThreadpoolTimer](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpooltimer) 与 [WaitForThreadpoolTimerCallbacks](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-waitforthreadpooltimercallbacks) 核对，最低 Vista，使用既有 Kernel32。编译需要 `_WIN32_WINNT >= 0x0600`；集中验证须覆盖默认 MSVC 和显式 Win7 LLVM/fixture 档位，不提高 SDK 的 Win7 最低系统要求。
