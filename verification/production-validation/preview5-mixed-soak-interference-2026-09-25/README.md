# preview.5 宿主八小时测试的外部负载观察

状态：**运行中观察，不是长测终态**。冻结测试进程 `SouiAcceptance` PID 10808 的 `events.ndjson` 以 `run.begin` 声明 28,800,000 ms 混合负载；本归档仅记录同时运行的其他虚拟机，不改写原测试事件或结果。原始长测目录在 `D:\SOUI-SDK-QEMU-validation\preview5-mixed-smoke-worktree-20260924\out\acceptance\8f747aca-5de9-448d-a695-18e714b79603\mixed-d8260ac9-e903-44f1-9090-42483361777a`。

系统进程观察分别发现 x64 QEMU 于 2026-09-25 02:33、02:39、03:26、04:41、04:52、05:01、05:52（Asia/Shanghai）启动；原始观察为 [1](external-load-observation-1.json)、[2](external-load-observation-2.json)、[3](external-load-observation-3.json)、[4](external-load-observation-4.json)、[5](external-load-observation-5.json)、[6](external-load-observation-6.json)、[7](external-load-observation-7.json)。04:53 的两台虚拟机同时占用约 5.5 GB 和 4.5 GB 工作集；05:11 与 05:54 的其他虚拟机分别占用约 5.5 GB 和 5.0 GB 工作集。此外，本仓库在长测期间进行了 SDK/模板增量构建及依赖审计，因此不能把这些时段当作隔离采样。观察点不是持续监控轨迹，不能推断虚拟机完整运行区间。八小时测试继续执行，以取得操作完成率、错误和最终退出证据；其资源趋势与性能数值不能直接用于生产门槛或 Electron 对照。若需要隔离资源结论，必须重新安排无外部负载的完整运行并冻结新输入。
