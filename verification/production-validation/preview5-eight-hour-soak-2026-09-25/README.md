# preview.5 x86 八小时混合生命周期运行

2026-09-24 23:37 至 2026-09-25 07:37（Asia/Shanghai），独立公共 SDK 消费者在单一进程执行固定混合序列。源码指纹 `b8355092795ee5bb08f36dc4c677c6259f5eb857032fc5cd8d3fdd9859a6d392`，运行 ID `d8260ac9-e903-44f1-9090-42483361777a`；这是 preview.5 的冻结载荷，不能作为 preview.6 的长期通过证据。监督器[正式结果](run/result.json)为 `passed=true`、退出码 0、无超时；[驱动终态](status.json)也为 `passed`。实际 28,802,459 ms 完成 1,690 轮、23,664 个步骤，包含页面 Bridge、重载、最小化/恢复、三窗口创建及关闭、空闲与最终 Runtime 退出。

已归档原始[请求](run/request.json)、[事件](run/events.ndjson)、[挂载阶段轨迹](run/events.ndjson.phases.ndjson)、结果、零输出的 stdout/stderr、SDK/消费者构建身份与[逐文件散列](artifact-hashes.json)。[分析器输出](analysis.json)从结果绑定的事件重新核对完整步骤和散列，按小时给出资源及操作时长。`resourceGate` 与 `longRunGate` 均为 `not-evaluated`，`releaseReady=false`。

每小时进程私有内存的中位数从第 0 小时约 64.1 MiB 升到第 7 小时约 136.7 MiB，p95 从约 99.6 升到 171.5 MiB；这是需要定位的增长信号。句柄中位数约 360–361、线程中位数约 15，挂载 p95 约 970–980 ms。运行期间至少多次与[外部 QEMU](../preview5-mixed-soak-interference-2026-09-25/README.md)及本机编译/磁盘操作重叠，**资源和时延没有隔离条件**。外部负载不能直接解释进程私有内存增长，也不能据此定性泄漏；须定位对象/缓存所有权并在隔离机器用最终候选重跑。

该测试没有真实键鼠输入、完整业务任务或 Electron 双端对照。操作完整且正常退出是当前冻结载荷的有限证据，不能放行 V05 资源门槛、生产长稳或 Windows 7 支持范围。
