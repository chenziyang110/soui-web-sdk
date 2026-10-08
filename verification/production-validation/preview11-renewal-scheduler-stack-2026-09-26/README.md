# 原动画加载停滞的 x86 原生调度证据

后续修复候选的 [x86 完整重编与原动画实测](../scheduler-fairness-x86-rebuild-2026-09-26/README.md)已退出 0。下面的旧内核失败样本保持原样，二者各自绑定输入。

本轮保持 preview.11 外部消费者的原页面动画、二进制和 60 秒就绪期限，使用[已修正计时竞态的监督器](../preview11-renewal-supervisor-clock-2026-09-26/README.md)。冻结文件 SHA-256 为 `8615b71960b35aa074553efd3ba6c4a7c91af594e88427cc1e59b153ec993749`，父冻结为 `b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef`。未修改旧输入。

## 观察

在 x86 消费者启动约十秒后，调试器只附加本次持有的进程，并在一次 `WebThreadImpl::schedulerTasks` 入口取得线程栈和计时器队列后退出。DLL 的 CodeView 与 PDB 都是 GUID `D47D4204-C2AE-4E48-A148-D7B5140B01F9`、age 4，日志确认加载私有符号。DLL SHA-256 为 `3b9071413f704958fb5708b8bb065785a6c3ed03b7fd0fb04f5952201e5fe4f7`；调试器退出 0，没有触发外层期限。

主线程队列共六项。上一帧时钟为 `1790403835.359981`；`HTMLParserScheduler::scheduleForResume` 的到期时间为 `1790403825.079492`、优先级 100，即相对于这一已记录时钟已经迟了约 10.280489 秒。队列里还有已到期的优先级 200 页面提交任务 `WebPageImpl::setNeedsCommitAndNotLayout`，以及 `Document::finishedParsing`。这是同一队列的真实快照，支持严格优先级使加载任务长期得不到执行的判断；不是由 DOMException 文本推断原因。

原生仍在 60,009 ms 报初始未就绪，60,540 ms 结束、退出 1；19 条日志连续，bootstrap、ready 和任务调用均为 0，宿主销毁及运行时退出完成。输入和运行载荷前后匹配；门禁未观察到其他桌面负载。调试器会影响时间，不将本轮资源或时延用于性能结论。

## 可复核资料与边界

[33 份原始文件](raw-manifest.json)包含冻结输入身份、启动命令、匹配符号记录、调试器全文、原生日志、原始退出码和[解析复核](raw/review.json)。可执行载荷和 PDB 保留在外部验证目录，不重复提交。第一次归档解析器误把函数名后的引号跨行匹配到文件名，退出 1；原解析脚本及失败日志保留，修正为单行匹配后重新分析同一日志，没有重新运行 SDK 或更改样本。

修复方向及边界见 [ADR-0016](../../../docs/adr/0016-scheduler-ready-task-fairness.md)，策略回归见[红绿测试](../scheduler-fairness-policy-2026-09-26/README.md)。必须以重编内核保持原动画复测才能判定实际修复，随后仍需两架构、真实应用、Win7、生命周期和资源回归。`realRenewalVerified=false`、`productionPass=false`。
