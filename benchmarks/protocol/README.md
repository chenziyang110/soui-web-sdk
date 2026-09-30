# 性能对照协议草案

当前状态：已执行 S01、S02 配对链路预检，正式性能验收尚未完成。固定 SDK 源码/运行包、Electron 精确版本与 lockfile、共享页面逐文件散列、设备系统与驱动、电源计划、刷新率、DPI、窗口内容区尺寸、输入轨迹、场景顺序及采样工具版本。两端均使用发布构建和正常安全设置，在同一设备上交替执行；未对齐的输入或视口使该轮不可比。

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

一轮宿主正常退出后，用 `node benchmarks/protocol/check-run-traces.cjs S01 <input-trace.ndjson> <process-tree.ndjson>` 复核两份原始轨迹。检查要求完整时长、相同 PID/进程创建时间、输入落在采样区间内、连续完整的资源样本、宿主退出码 0，并拒绝短探针和超时。S01/S02/S04 还要求场景操作实际持续执行，S05 要求启动动效；仅等待足够时长后点“结束”不能算有效输入。当前 S03 一律拒绝，直到补上真实 IME composition 证据链；单改轨迹里的布尔字段不能证明输入法路径。该检查不读取页面结果，也不测物理呈现；必须再运行页面配对检查，并补足散列、运行顺序、重复次数与呈现追踪。

`run-one.cjs` 将一个可见的宿主、`ProcessTreeSampler` 和 `InputTrace` 串行绑定为一轮真实场景；输出目录必须是新的绝对路径。每轮先复制共享页面到独立目录，逐文件验证副本与输入相同，宿主只加载副本，结束后再次验证副本未变。宿主仅在页面提交完整结果后正常退出，工具保存三个退出码、原始日志、页面结果和逐文件页面身份，失败时也保留独立目录。失败清理可核对本轮直接子进程的 PID、父进程和可执行文件身份，但一律只停止已持有的子进程句柄，并在 `run.json.cleanup` 标明后代清理未证实；父 PID 可能复用，不能仅凭直接子进程身份安全使用整树终止。等待子进程 `close` 后才固定退出记录，避免过早读取未关闭的日志。SOUI 的最后一个可选参数是已安装 SDK 静态库，Electron 的是 `package-lock.json`；正式身份预检必须提供它。两端按同一页面目录交替执行，例如：

```powershell
node benchmarks/protocol/run-one.cjs soui S01 <同一页面目录> <SouiBenchmark.exe> <node.dll> <ProcessTreeSampler.exe> <InputTrace.exe> <新的SOUI输出目录> <已安装的soui_sdk.lib>
node benchmarks/protocol/run-one.cjs electron S01 <同一页面目录> <electron.exe> <Electron应用目录> <ProcessTreeSampler.exe> <InputTrace.exe> <新的Electron输出目录> <package-lock.json>
node benchmarks/protocol/check-pair-runs.cjs <SOUI输出目录> <Electron输出目录>
```

组合检查重新计算两端轨迹、原始页面文件哈希、完整页面资源、输入动作序列、运行机器和宿主身份，再调用页面配对检查。它通过仍只表示配对**预检**成立；呈现、启动、重复性和目标平台仍是独立门槛。2026-09-25 首对 S01 完整时长样本的页面和轨迹均通过，但因当时未冻结全部页面资源和宿主身份，组合检查明确拒绝，见[原始归档](../../verification/production-validation/benchmark-s01-preflight-2026-09-25/README.md)。随后 [S02 三对预检](../../verification/production-validation/benchmark-s02-full-preflight-2026-09-25/README.md)通过，两对无 QEMU 并行负载，但仍未达到五对重复及物理呈现门槛。

`run-series.cjs` 在一台 Windows 设备上按 SOUI→Electron、Electron→SOUI 交替执行指定场景，默认五对；系列开始时先冻结完整共享页面，每个宿主都从该副本运行上述单轮监督器，每对重新计算完整预检，并跨轮核对机器、页面、宿主与工具身份。输出是新的绝对目录，保留每轮原始结果、逐对预检和 `series.json`，失败或中途停止不会补写未执行轮次。它在每轮开始和结束时查询已知会干扰的 QEMU、验收应用、编译器、独立采样器、输入驱动及其他基准宿主进程；进程清单为空或含无法解析的行时拒绝继续。单轮运行期间还每五秒按进程父子身份识别本轮预期的 SOUI/Electron 宿主、采样器与输入器；收养子进程时核对其创建时间不早于父进程，避免复用的父 PID 将旧外部负载误记为本轮子进程。即使 QEMU 或编译器由本轮子进程启动，也判作干扰，并将观察结果写入 `live-interference-gates.ndjson`。每次实时检查还须确认本轮运行器的 PID 与创建身份，查询不到运行器或门禁日志无法写入都拒绝该轮。发现外部负载或查询失败时，系列保留失败原件并拒绝计入有效配对；清理时仅停止已持有的本轮运行器句柄，并在记录中标记后代清理未证实，下一轮前须人工核查和确认没有残留；PID 与创建时间核验仍用于隔离判定，不授权整树终止。五秒采样仍可能漏掉在两次采样间完整启动并退出的短负载，正式性能结论仍须人工隔离设备。S03 的外部驱动尚不能证明真实中文 IME composition，因此系列入口明确拒绝 S03。

门禁也将 `Git.exe`、独立模板应用 `ScrollStarter.exe` 和验收输入探针 `V02InputProbe.exe` 视为竞争负载。这三类进程即使来自本轮进程树，也不能按预期基准宿主、采样器或输入器豁免。系列首尾和轮内检查使用同一分类，防止开始时空闲而后续 Git 操作或其他交互验收混入样本。进程名门禁不能发现所有未知负载，仍需人工隔离设备；旧记录不能因为后来补齐分类而追认为隔离通过。

首次系列实跑在 Electron 结束前约 0.67 秒检测到另一个 QEMU 启动，[保留的停止证据](../../verification/production-validation/benchmark-s02-series-interference-2026-09-25/README.md)显示两端单轮完整但系列拒绝计数。完成五对且无已知并行负载仍只是系列预检，不能自动关闭物理呈现或生产门槛。

```powershell
node benchmarks/protocol/run-series.cjs S02 <共享页面目录> <SouiBenchmark.exe> <node.dll> <electron.exe> <Electron应用目录> <ProcessTreeSampler.exe> <InputTrace.exe> <新的系列输出目录> <已安装的soui_sdk.lib> <package-lock.json> 5
```

系列原始文件全部保留后，可用 `node benchmarks/protocol/summarize-series.cjs <已完成的系列输出目录绝对路径>` 生成一次性 `series-diagnostic-summary.json`。该诊断入口重新读取每轮原始页面、输入和资源轨迹，重新计算逐对预检，并核对冻结页面、系列首检和每轮前后检查、五秒实时门禁、交替顺序及至少五对有效样本；实际运行起止时间还必须按交替顺序排列且互不重叠，每轮前后门禁须夹住对应运行。它核对系列记录、`run.json` 与实时门禁中的运行器 PID 和创建身份一致、每轮起止时间附近均有门禁采样、至少一次采样落在实际运行时间内，且采样间不存在超过 15 秒的缺口。缺失或受干扰的记录会使汇总失败，不给出比值。成功时给出逐轮资源和页面/输入时长的分布及候选资源比值；它不测真实屏幕呈现、冷/热启动或完整运行包，也不将 `benchmarkValid` 或 `releaseReady` 设为通过。不能拿 JS 回调间隔或注入时间代替物理输入到呈现时延。
