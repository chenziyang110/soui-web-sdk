# 性能对照协议草案

当前状态：测量协议，尚未执行。固定 SDK 源码/运行包、Electron 精确版本与 lockfile、共享页面逐文件散列、设备系统与驱动、电源计划、刷新率、DPI、窗口内容区尺寸、输入轨迹、场景顺序及采样工具版本。两端均使用发布构建和正常安全设置，在同一设备上交替执行；未对齐的输入或视口使该轮不可比。

每轮保存宿主元数据、页面原始结果、外部输入时间戳、进程树资源样本、退出码与超时、运行包文件清单及散列。启动冷态至少 10 次、热态至少 30 次；交互每场景至少 5 轮、每轮 60 秒。报告 p50/p95/p99、样本数、分布和异常样本，不只给平均值。S05 的 120 秒动效检查应独立保留，不能用不足时长的样本填补 60 秒交互结果。

页面 `eventLog` 是渲染器收到事件的时间，`rafIntervalsMs` 是 JS 回调间隔；二者都不是物理输入到屏幕呈现时间。未取得系统输入到呈现追踪时，该指标标为“未验证”。必须另外采集实际呈现、整进程树 Private Bytes/CPU、可见与最小化空闲、冷/热启动至可交互，以及包含全部运行依赖的压缩和解压包大小。

只有场景完整、输入和视口匹配、版本与载荷可追溯、无超时且原始文件可校验的配对轮次才进入比较。无法等价运行的功能标记“不可比”，失败和缺失标记“未通过/未测”；不得把小包或主观流畅性折算成生产准入。

`ProcessTreeSampler` 是独立的 Win32 原始采样器，不链接 SDK 或 Electron。以进程 PID、采样间隔（毫秒）、最长时长（毫秒）、新的绝对 NDJSON 路径启动；每轮枚举该进程及后代，并继续追踪已观察到、但父进程先退出的后代，记录逐进程和整树 Private Bytes、工作集、句柄、线程，以及相邻样本的整机容量 CPU 百分比。首个样本 CPU 为 `null`；进程无法读取时 `complete=false` 并以非零码结束，超时以码 2 结束。采样器正常结束仅证明原始记录完整，应用退出码仍须由监督器单独判定。短命子进程仍可能在两次采样之间完整创建并退出，因此还需检查宿主进程事件与采样间隔；结果不得单独充当完整性能报告。

```powershell
cmake -S benchmarks/protocol -B out/benchmark-sampler -G "Visual Studio 17 2022" -A x64
cmake --build out/benchmark-sampler --config Release
out/benchmark-sampler/Release/ProcessTreeSampler.exe <PID> 1000 65000 <新的绝对结果路径>
$env:PROCESS_TREE_SAMPLER_EXE = (Resolve-Path out/benchmark-sampler/Release/ProcessTreeSampler.exe).Path
node --test tests/benchmark/process-tree-sampler.test.cjs
```

`InputTrace` 是正式宿主外部的 Win32 输入驱动，接受宿主 PID、`S01`–`S05`、新的绝对 NDJSON 路径和 `full`/`probe` 模式。它先定位可见的 1100×720 内容区窗口，固定窗口位置并要求其处于前台；通过 `SendInput` 发出鼠标点击、滚轮及 Unicode 按键，同时保留每次输入前后的 QPC 时间。S04 的窗口尺寸变化单独标为 Win32 操作。`full` 为 S01–S04 保留至少 61 秒、S05 至少 123 秒；`probe` 仅持续 3 秒，不能进入性能样本。输出只是外部注入轨迹；页面事件、动作、视口与结果须单独核对。S03 的 Unicode 按键**绕过中文 IME composition**，因此 S03 的 IME 门槛仍需真实输入法专项验证；驱动本身不能提供物理呈现时间。

```powershell
cmake --build out/benchmark-sampler --config Release --target InputTrace
$env:BENCHMARK_INPUT_TRACE_EXE = (Resolve-Path out/benchmark-sampler/Release/InputTrace.exe).Path
node --test tests/benchmark/input-trace.test.cjs
out/benchmark-sampler/Release/InputTrace.exe <宿主PID> S01 <新的绝对结果路径> full
```
