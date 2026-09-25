# S02 系列入口并行负载拒绝实测

2026-09-25 用 `run-series.cjs` 执行一对 S02：先冻结完整共享页面，再按 SOUI→Electron 顺序各运行约 62 秒。两端宿主、外部输入和整进程树采样均退出 0；独立[页面、输入和身份预检](pair-preflight.json)重新计算为 `preflightAccepted=true`。这些只能说明两份原始数据完整，不能单独形成性能样本。

系列[原始状态](raw/benchmark-s02-series-20260925-run1/series.json)为 `stopped`，没有任何 `preflightAccepted` 的系列配对。新的 QEMU 进程 PID 57168 于北京时间 **09:58:42.125** 启动，见[事后进程观察](post-observation.json)；Electron 轮次直到 **09:58:42.792** 才结束，因此最后约 0.67 秒与 QEMU 重叠。系列入口在轮次终点检查时发现并行进程，拒绝把该对计入隔离样本，保留[完整原始目录](raw/benchmark-s02-series-20260925-run1/)以供复查。

发现此情形后，系列入口已改为把触发停止的进程名和 PID 连同检查时间写入后续 `series.json`。这次原始状态保持运行时版本，不补写事后字段。更严格的实时负载隔离、五对重复、实际屏幕呈现及启动样本仍缺失；`benchmarkValid=false`、`releaseReady=false`。
