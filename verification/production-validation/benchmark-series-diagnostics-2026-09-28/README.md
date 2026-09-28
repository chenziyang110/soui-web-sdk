# 基准系列门禁与诊断汇总：代码级验证

本目录保存 `codex/sdk-benchmark-series-diagnostics` 对应源码在完整仓库工作树中的原始测试记录。当前只验证系列驱动的外部负载门禁、进程身份检查，以及诊断汇总对缺失、截断或污染记录的拒绝行为。没有在本次执行 SOUI 与 Electron 的五对实测，也没有得到物理输入到呈现、冷/热启动、完整产品包大小或生产价值结论。

增加系列、单轮原始记录与实时门禁的进程身份交叉核对后，2026-09-28T03:00:00Z 至 03:00:05Z 执行 `node --test tests/benchmark/series-interference.test.cjs tests/benchmark/series-summary.test.cjs`，原始退出码 0；24 项通过、0 项失败、0 项跳过。七个相关文件在执行前后 SHA-256 相同，逐文件散列见 [当前代码结果记录](raw/final-attempt5/result.json)。[原始标准输出](raw/final-attempt5/stdout.txt) 与 [原始标准错误](raw/final-attempt5/stderr.txt) 保留在此；外部原件位于 `C:\SOUI-SDK-production-validation\benchmark-realtime-gate-20260928\full-tree-tests-attempt5`，复制时逐字节散列一致。同一次独立 Windows PowerShell 查询原始退出 0，实际进程创建时间被序列化为 [`/Date(毫秒)/`](raw/final-attempt5/powershell-createdat.json)；汇总器同时接受这一格式与 ISO 时间。较早的 [attempt1](raw/result.json)、[attempt2](raw/final-attempt2/result.json) 和 [attempt3](raw/final-attempt3/result.json) 原样保留。[attempt4](raw/binding-attempt4/result.json) 的 24 项中有一项测试夹具漏传 `host` 参数而原始退出 1；原失败和修正后的 attempt5 分开保留。

`run-series.cjs` 仍只按预检结果生成 `series.json`，检测到外部负载或实时门禁日志写入失败时停止自身轮次；写入失败无法保存该条门禁原件，系列总记录仍须标记停止。新增的 `summarize-series.cjs` 只在至少五对完整且交替的样本、冻结身份、逐对原始预检和实时门禁都可复核时生成诊断结果；它还要求单轮记录中的自身 PID 与系列记录及门禁的 PID、创建身份一致，每轮运行起止区间内的采样间隔不得超过 15 秒。即使诊断成功，输出中的 `benchmarkValid` 与 `releaseReady` 仍为 `false`。实时检查每 5 秒采样，可能漏掉采样间完整启动并退出的外部进程，所以正式资源和时延结论仍需隔离设备及真实完整测试。

本次测试执行时本机存在他人运行的 QEMU；测试自身只做短时进程查询与隔离的测试子进程，不运行图形基准，因此这里的记录不能用于性能比较。完整五对交替测试、目标系统、实际呈现和产品价值门槛仍待执行。
