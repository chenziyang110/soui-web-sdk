# S01 同页面完整时长配对链路预检

2026-09-25 在同一 Windows x64 宿主上，用相同的 `dist-legacy` 页面和外部 `InputTrace` 对 SOUI、Electron `44.4.4` 各执行一次 S01。原始输入、整进程树采样、页面结果、三个进程的退出码和 stdout/stderr 分别保存在 [raw/](raw/)；本记录是工具链预检，`benchmarkValid=false`、`releaseReady=false`。

SOUI 的 [run2](raw/benchmark-s01-soui-20260925-run2/run.json) 和 Electron 的 [run3](raw/benchmark-s01-electron-20260925-run3/run.json) 都持续注入输入约 61.8 秒，宿主、输入驱动和采样器均退出 0，各自的轨迹完整性通过。独立[页面配对检查](page-pair.json)确认场景、10,000 行 fixture、视口、动作序列和完整时长一致。更严格的[组合预检](pair-preflight.json)仍为 **未接受**：这两轮启动时只固定 `benchmark.html`，没有逐文件冻结完整页面资源和静态 SDK 库/Electron 应用源身份；物理输入到呈现、冷/热启动与至少五轮交替重复也未测。不得从这对样本推导流畅度或内存优势。

失败样本保留原状：[SOUI run1](raw/benchmark-s01-soui-20260925-run1/run.json) 因宿主启动器把 GUI 窗口隐藏，驱动在 45 秒内找不到可见窗口；[Electron run1](raw/benchmark-s01-electron-20260925-run1/run.json) 与 [run2](raw/benchmark-s01-electron-20260925-run2/run.json) 的默认原生菜单令 Win32 客户区成为 1100×746，驱动按冻结的 1100×720 口径正确拒绝。后续轮次把宿主窗口保持可见并移除 Electron 默认菜单，不改变 Electron 的沙箱、上下文隔离、Node 集成和 Web 安全设置。

后续正式轮次使用增强的 [`run-one.cjs`](../../../benchmarks/protocol/run-one.cjs) 在开始和结束时逐文件固定页面资源，并记录 SDK 静态库或 Electron 锁文件身份；独立 [`check-pair-runs.cjs`](../../../benchmarks/protocol/check-pair-runs.cjs)再次核对原始轨迹、页面和身份。S01 本次成功轮次不能补写运行时尚未采集的身份字段，应重新执行。
