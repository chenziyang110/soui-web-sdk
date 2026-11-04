# SDK 性能 M01 手动执行说明

以下命令从仓库根执行。第 1–4 节是原 BurstInput 路径：采样会启动本任务窗口并发出真实桌面输入，必须由操作者在隔离可见桌面执行；冻结、编译与无目标预检不注入输入。第 5 节是独立的 manual-input/profile 路径，不启动 BurstInput，也不具有确定回放资格。任何冻结步骤都不自动启动窗口。不要启动 Electron。

## 1. 构建和身份

1. 安装或选择冻结基线 SDK，记录来源提交、构建报告、实际 `soui_sdk.lib` 与匹配头目录；候选另建安装目录。独立 consumer 用 `SOUI_INPUT_UI_SDK_VERSION` 精确选择版本。示例：

   ```powershell
   cmake -S benchmarks/input-ui-host -B out/input-ui-host -G "Visual Studio 17 2022" -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.19041.0' '-DCMAKE_PREFIX_PATH=<冻结SDK安装绝对路径>' '-DSOUI_INPUT_UI_SDK_VERSION=0.6.0'
   cmake --build out/input-ui-host --config Release
   ```

2. 工具使用独立、冻结 source owner 的 `tools/internal/Build-ValidationTools.ps1` 构建并封印；不能把旧 `measurement-tools.json` 指向修改后的 burst-input.cpp。LLVM profile 使用现有显式六个工具路径，不自动降级/升级工具链。完整 receipt 必须由 `measurement-validation.preparedAt(owner,architecture,options)` 验证。源码一致性证明不等于同用户不可伪造的认证。
3. 保存 host build 原件（configure/build日志、编译参数、源码散列、SDK安装清单与EXE哈希），作为 `host_build_evidence`。冻结请求的 hash 只绑定这份证明，不会自动把其内容证明为真实编译成功。记录 `source_commit` 与 dirty patch 身份，未提交候选不能只写 HEAD 冒充纯提交。

## 2. 请求字段和冻结

建立新的 `request-template.json`，schema 为 `soui.input-ui-run-request/v1`；所有路径为绝对路径，提交为40位小写hex。必需字段：

| 字段 | 内容 |
|---|---|
| product_id / source_commit / measurement_source_commit | 同产品标识、SDK源码提交、测量源码提交；dirty变化由build evidence与本轮measurement_sources散列另记 |
| architecture / runtime_version | x64或x86、实际内核版本 |
| host_executable / sdk_library / miniblink_binary / host_build_evidence | 同一冻结消费者、SDK LIB、匹配DLL、host构建证明文件 |
| measurement_owner / measurement_toolchain_request | 独立工具owner；LLVM用该owner内部request的绝对路径，MSVC省略后者 |
| input_executable / owned_runner | 上述owner收据中的BurstInput与OwnedProcessRunner，不接受其他目录旧工具 |
| page_root | 含固定burst-page.html/js的目录 |
| trace_mode / kind / hz / count | full或e2e；pointer_move或wheel；125/500/1000；30秒采样使用hz×30 |
| frozen_environment / frozen_configuration | 原有v1环境与配置JSON；环境真实记录显示、DPI、物理设备、隔离和失效原因，不把未确认值填为通过 |
| calibration_template | 原有soui.burst-calibration-template/v1，真实QPC frequency、device_pixel_ratio=1、实际observation_error_ms |

```powershell
node benchmarks/protocol/freeze-input-ui-request.cjs '<模板绝对路径>' '<全新冻结请求绝对路径>'
```

冻结校验失败时保留旧原件并修正来源；不能改 hash 让旧 receipt 通过。新 request 包含 v2 `frozen_inputs`；它会绑定当前host源码、protocol工具/reader源码和实际载荷。冻结后任何输入变化都要求新 request。

## 3. 人工采样与最小场景

操作者确认只运行一个负载、显示器拓扑与电源模式不变、桌面已解锁、窗口完整可见，再执行：

```powershell
node benchmarks/protocol/run-input-ui.cjs '<冻结请求绝对路径>' '<全新结果绝对目录>'
```

本入口会启动真实公共 MiniBlink Host，并先核对 ready 的 formal 标识，再启用拥有准确 PID/创建时间的输入器。输入器仍会要求目标位于前台；失焦、遮挡、退出或身份变化都是保留原件的失败，不能靠 focus suppression 通过。不要手动改 host-ready/result。

先10秒预热；短 full 归因一次，再以 e2e 做A/B/B/A 30秒（e2e为scalar底噪，报告实际模式）。有明确新变动/失败才重测。基线波动不可区分时最多每配置三轮，仍不确定即停止。原reader中历史五对门槛不因本轮短验自动改为已通过。

| 场景 | 当前可执行入口 | 仍需记录 |
|---|---|---|
| 轻输入反馈 | burst pointer_move，125Hz | SDK接纳/handler、T7捕获图像上界、终态；非物理光子延迟 |
| 滚动与高频拖动 | burst wheel / pointer_move，500/1000Hz | 合并与未解决输入、队头年龄、focus/capture；人工打字与真实IME另验 |
| 固定重绘 | 暂用同一burst页面受输入改变的真实内容 | 连续无输入动画/FPS专用页尚待接入；不能把marker变化叫场景FPS |
| Bridge 压力 | WebView2/Bridge lane另交付64B/4KiB/近上限固定工作量 | 当前M01 burst runner不伪装已覆盖；prep/owner两段分别观测 |
| 多Host/切换 | 人工目标机按SDK集成宿主验证 | 两Host公平、导航、隐藏恢复、DPI/resize与积压关闭尚待实际验证 |

记录本轮CPU、私有字节/完整进程树、queue p95/p99、long frames、copy字节和包体积；无法得到的项填 unknown。PresentMon/ETW须先证明覆盖实际呈现路径；rAF、提交、desktop图像观察与physical presentation分别命名。当前burst分析不提供完整资源报告，不能从其check通过推出性能全部达标。

## 4. 不依赖桌面的验证

```powershell
node --test tests/benchmark/input-frame-trace/*.test.cjs
cmake -S tests/benchmark/input-frame-trace -B out/input-ui-policy -G "Visual Studio 17 2022" -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.19041.0'
cmake --build out/input-ui-policy --config Release
ctest --test-dir out/input-ui-policy -C Release --output-on-failure
```

设置 `BENCHMARK_BURST_INPUT_EXE`、`BENCHMARK_OWNED_RUNNER_EXE`、`BENCHMARK_INPUT_UI_HOST_EXE` 为新编译工具后，native-preflight只验证无目标、错误创建时间和缺配置会在创建窗口/注入前拒绝；不设置则skip，skip不是通过真实输入。真实mounted Host的focus阻塞反例仍需独立Engine hold信号/watchdog与授权桌面执行，当前options单测不能替代它。

## 5. 独立的手动输入与 0/1000/2000 profile

这一入口用于同一实际 Host EXE 的冷预算诊断，不替换旧确定回放协议。它使用本节对应源码新编译的 EXE；历史未带 `--manual-input` 的 EXE 仍会执行默认指针定位，不能拿来冒充此入口。操作者显式启动后自行将窗口置前、移动指针或滚轮，完成后点击“完成记录”，等待“已接纳，尚未落盘”，再正常关闭窗口；工具不启动 BurstInput，不注入输入，不自动切换前台。输入数量、节奏、遮挡和观察窗口没有受控回放证据，均不因此获得性能或物理延迟资格。

创建独立 `soui.input-ui-profile-request/v1` 模板。新工具严格拒绝未知字段：完整字段以 `benchmarks/protocol/input-ui-profile.cjs` 的 `REQUEST_KEYS` 与校验为准。核心绑定包括 `experiment_id`、`architecture`、`profile.engine_budget_us`（仅 0/1000/2000）、`input_mode:manual`、`trace_mode:e2e`、匹配 EXE/LIB/header/DLL、SDK raw/numeric 版本、实际 build evidence、完整 runtime/page 目录、OwnedProcessRunner 来源和准备收据、环境/配置及人工工作指导。`required_sources` 固定为 sdk/kernel。

`source_manifest` 使用 `soui.input-ui-profile-source/v1`，记录真实 `source_commit`、`measurement_source_commit`、`dirty` 和文件 `{path,bytes,sha256}`；至少含 main、trace_capture.cpp/.h、harness_options、profile_options、manual_report.h、manual-observation-gate.h、CMakeLists、sdk_version 这九份原生输入及独立 `benchmarks/protocol/manual-input-page.html/js`。即使不传可选 gate 参数，也必须绑定实际编入 main 的该依赖。不能用相同 HEAD 隐藏 dirty 源码，也不能为了比较三档预算伪造提交。构建证明、安装头库和运行依赖另行绑定。三档只改变预算；GPU 两项仍为 null，客户区保持 1100×720。其他参数改变必须成为新的实验。

`page_root` 必须含这两份新的 manual-input 页。它接受任意方向/位置鼠标移动及正负滚轮，显示本地 JS 事件计数和坐标，以单待处理 rAF 合并绘制；没有自动输入或空闲绘制循环。点击完成才冻结 `souiManualInputResult()`，保留溢出、错误事件、blur/hidden/resize、几何异常及 `render_pending`。原生通过现有受授权 Bridge 一次接纳，不在输入热路径写盘。快照原文及文件各≤8 KiB；严格拒绝重复快照 key、未知字段、错误类型和越界；页面不能指定输出路径或原生身份。外层 envelope 沿用既有 SDK 语义。新页面没有注入序号、CRC、T7或确定回放资格，不能与旧 burst 页混配。

`accepted` 只表示内存接纳，不表示 persisted。正常 Host/Runtime 关闭、producer 静止后才在原共享关闭预算内 CREATE_NEW/写入/flush `manual-input-result.json`；host-result 单列 `manual_report_submission` 与 `manual_report_persisted`。已接纳报告写盘失败、文件碰撞或超时使进程非零，保留原件。未点击时明确为 unsubmitted/null/unknown，不能填零。不要在收到接纳回执前或用强杀代替正常关闭。页面快照只覆盖点击时点；即使失焦计数为零，也不证明连续前台或等工作量；Canvas renders 不是 unique frames/FPS。报告不保存 session/token，原生 run/PID/creation/QPC 与页面自报状态分开。

冻结只生成新文件，不启动程序：

```powershell
node benchmarks/protocol/input-ui-profile.cjs freeze '<模板绝对路径>' '<全新冻结请求绝对路径>'
```

只有操作者决定开始时才运行下列命令；每档使用全新应用进程和结果目录，不能运行中热切预算。不要同时运行三档：

```powershell
node benchmarks/protocol/launch-input-ui-profile.cjs '<冻结请求绝对路径>' '<全新结果绝对目录>'
```

Launcher 只持有一个本任务 Host Job，按真实 PID/创建 FILETIME 核对 ready/终态，不启动输入器。新 launcher 要求 ready 的 `manual-page-snapshot-v1` 能力，拒绝旧 EXE；历史原件无此能力仍按 unknown 读取，不补证据。人工约 30 秒操作后点击完成、等待接纳，再正常关窗；Host 原活动期限 150 秒、关闭共享剩余预算 20 秒保持，外层 190 秒 watchdog 只负责有限失败收尾。任何强制结束、缺终态或文件变化都不能转成通过。`--engine-budget` 和 child env 冷值必须相同；文件中 `engine_effective_observed:false` 表示没有公共 Engine getter，不能拿 env 读回伪称已观察实际生效预算。

新 reader 还要求 ready/result 的 `compiled_sdk_version`/`compiled_sdk_package_version`（从实际编译头的宏写入 EXE）都与请求 raw/numeric 相同；仅修改外部 header/lib 标签不能让旧 EXE 被当成新 SDK。该旁证没有认证实际加载 kernel，后者仍按未观测边界记录。

关闭后可只读检查或比较（最多六份）：

```powershell
node benchmarks/protocol/input-ui-profile.cjs check '<结果绝对目录>'
node benchmarks/protocol/input-ui-profile.cjs compare '<预算0结果目录>' '<预算1000结果目录>' '<预算2000结果目录>'
```

初末 scalar 覆盖进程生命周期；uint64 按 BigInt 差分，64 桶保留原边界，分位只能叫桶上界。工具不能减两个分位数来构造 provider 前缀，也不能把全生命周期差分裁成操作者的 30 秒。缺源、计数倒退、故障、未静止、clock 错配、写盘或自然退出失败，均保留原件并拒绝资格。即使 scalar 完整，人工工作量仍是 `workload_comparable:unknown`，`performanceGate:false`、provider 前缀与物理输入延迟仍未测；它只帮助定位嫌疑，不决定 I02 默认档位或证明 I03 收益。

新增无窗口本机合同可显式构建/执行；下面不是 GUI 采样命令：

```powershell
cmake --build '<独立Host构建目录>' --config Release --target SouiInputUiProfileContracts
& '<独立Host构建目录>/Release/SouiInputUiProfileContracts.exe' '<全新绝对合同证据目录>'
```

合同使用真实公开 recorder/exporter 与限定的 fault source，验证选项和文件/完整性边界。实施、编译、合同执行与实际人工输入是分别记录的门槛；不能从源码已落地推断其余均已完成。

带外部 observer 的完成门流程与纯三档 profile 分开：使用专门绑定控制器/Host/独立 Control 的入口，等被持有 observer 自然结束才发 gate 信号，然后再人工点击完成并正常关窗。门只证明程序结束，非零 observer 仍失败；页面快照也不代表精确30秒边界。完整身份、pending 重取与关闭契约见 [原生观察完成门](README.md#原生观察完成门)。旧不带 gate 的 profile 入口不会凭空获得这项保证，不能用其接纳回执判断 observer 已结束。
