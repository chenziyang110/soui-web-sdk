# preview.5 宿主八小时测试的外部负载观察

状态：**运行中观察，不是长测终态**。冻结测试进程 `SouiAcceptance` PID 10808 的 `events.ndjson` 以 `run.begin` 声明 28,800,000 ms 混合负载；本归档仅记录同时运行的其他虚拟机，不改写原测试事件或结果。原始长测目录在 `D:\SOUI-SDK-QEMU-validation\preview5-mixed-smoke-worktree-20260924\out\acceptance\8f747aca-5de9-448d-a695-18e714b79603\mixed-d8260ac9-e903-44f1-9090-42483361777a`。

系统进程观察分别发现 x64 QEMU 于 2026-09-25 02:33、02:39、03:26（Asia/Shanghai）启动；原始观察为 [1](external-load-observation-1.json)、[2](external-load-observation-2.json)、[3](external-load-observation-3.json)。这些样本足以否定相应时段的“独占宿主资源/时延”前提，但不是持续监控轨迹，不能推断虚拟机完整运行区间。八小时测试继续执行，以取得操作完成率、错误和最终退出证据；其资源趋势与性能数值不能直接用于生产门槛或 Electron 对照。若需要隔离资源结论，必须重新安排无外部负载的完整运行并冻结新输入。
