# SDK 双后端手动性能消费者

本工具只依赖公共 SDK，用同一 ES5 页面和相同原生应用逻辑比较基线/候选 MiniBlink、WebView2。它属于 SDK 工作负载工具，不是 Scratch 或业务 Demo。默认须操作者显式运行；Agent 编译不会启动窗口、注入输入或切换前台。

## 固定工作量 Bridge 模式（新资源版本）

“固定工作量：1024次Bridge”按钮使用当前载荷（64、4096或60000字节），先执行32次同配置暖机，再发送恰好1024次真实echo，最多8个在途。成功必须逐条核对sequence和完整payload，无失败重试；失败、超时、错包、待回和未知原生终态分别保留。原60秒整轮期限不因批次/暖机重置，停止后只做有界drain；页面等待超时不能证明原生工作已经取消。未知终态会阻止新固定工作和页面生命周期操作，须正常关闭并重启Host。

正式测量不运行rAF或周期UI仪表。续发优先使用有界单后继MessageChannel宏任务，每批最多8次；缺能力时显式回退4ms timer并记录primitive、fallback和yield成本。相同primitive/载荷/并发/成功操作数才能配对。报告schema为 `soui.sdk-performance-fixed-work/v1`，记录完成耗时、成功操作吞吐、RTT及完整计数；它不是FPS、独立原生CPU成本或物理输入到显示。旧 `soui.sdk-performance-page/v1` 的30秒记录仍保持原语义，不能放宽旧分析器来混读两个schema。

固定模式要求 `--observation=off --input-trace=off` 和W02输出时间观测关闭；配对建议相同 `--frame-rate=60`，并检查起止窗口、Runtime和pacer配置。选择载荷后先暂停普通场景再点固定按钮；结束后用原“保存报告”并等待writer实际saved。保存、排序、预览与身份查询不计入正式1024次区间。源码资源变化另冻结pageHash，可复用C++/SDK输入完全未变的EXE；既有r2资源包与历史结果不覆盖。

覆盖五类入口：空闲键盘/IME/输入反馈，固定 240 角色 Canvas 动画，固定小/4 KiB/近 64 KiB Bridge echo，pointer/scroll，双 Host/导航/隐藏恢复。点击“记录 30 秒”才开始记录；隐藏或失焦使记录 invalid。rAF、有效 scene step、确定性状态指纹和包含 DOM 修改的事件处理器执行时间分别记录，均不能称为实际呈现 FPS 或物理输入延迟。

另有真 idle 入口：页面没有周期 rAF/DOM 观察器，仅保留记录结束的一次 timer，rAF Hz 为 null；输入场景明确包含 rAF 观察器。Canvas 每 callback 最多追赶 8 个模拟步，保留积压，不通过丢弃时间把低帧率伪装成更少工作；状态指纹为整数状态 FNV-1a，不是像素 CRC，只有种子与 step 相同时可比较。重复绘制同一模拟状态不算新的视觉帧，`visualChangeFrames` 和 `workloadComparable` 保持未知。Bridge 最多 8 个未结调用，每批最多 8 次并至少 4 ms 让出任务；timeout 只结束页面等待，不冒称取消 native work。

## 构建隔离

基线和候选必须分别安装，再从各自安装的头文件、静态库和 Loader 编译同份消费者。禁止用当前源码头覆盖旧 SDK 的类布局。`SOUI_PERF_OPTIONAL_FRAME_FEEDBACK=OFF` 可编译不含新虚函数的 0.6.0；候选打开该选项时编译器必须确认对应 override。新反馈只发布有界原子状态；帧 pacer 的停止/重启只在 UI owner 执行。

从仓库根执行，路径按冻结产物调整：

```powershell
cmake -S benchmarks/sdk-performance-host -B ../perf-host-baseline `
  -G 'Visual Studio 17 2022' -A x64 -T v143 `
  '-DMATCHED_SDK_ROOT=../baseline-install' '-DMATCHED_SDK_VERSION=0.6.0' `
  '-DMATCHED_SDK_BUILD_RECEIPT=../baseline-build/build-Release.json' `
  "-DMATCHED_WEBVIEW2_SDK_ARCHIVE=$((Resolve-Path ../webview2-deps/microsoft.web.webview2.1.0.4258.31.nupkg).Path)" `
  -DSOUI_PERF_BUILD_IDENTITY=a4fc-baseline -DSOUI_PERF_OPTIONAL_FRAME_FEEDBACK=OFF
cmake --build ../perf-host-baseline --config Release --parallel 2
# 候选使用独立安装目录、独立 build、实际数值 package version，并设 feedback=ON；页面不修改。
node --test benchmarks/sdk-performance-host/tests/page-contract.cjs
node --test benchmarks/sdk-performance-host/tests/sdk-build-identity.cjs
```

Windows SDK 必须为 10.0.19041.0，具体锁定方式沿用宿主工具链；CMake 配置会拒绝选到其他 Windows SDK。静态库 /MD Release 与消费者宏布局保持一致。构建不添加默认 GUI CTest。

`MATCHED_SDK_BUILD_RECEIPT` 必须是该安装载荷对应、已通过的 SDK-only `soui.source-build/v2` 收据。配置时核对数值版本、架构、配置、源码清单自身散列、每份安装公共头和两份静态库；安装头的 CRLF 按收据既有 LF 规范核对。完整安装前缀另保存逐文件散列至 build 目录 `sdk-build-inputs.json`，每次编译前重新核验，任何输入或收据变化均要求重新配置消费者。报告的 `identity.sdkBuild` 内嵌源码/收据/安装闭集散列及静态库身份；`buildIdentity` 只作显示标签，不再承担源码绑定。该摘要不包含开发机绝对路径；它证明编译输入，不授予性能或生产资格。基线必须提供自己的真实收据，不使用候选头文件或手填标签代替。

## 手动运行

`MATCHED_WEBVIEW2_SDK_ARCHIVE` 指向本地已有的、与安装前缀中 source-bound dependency.json 对应的 NuGet 包；配置不会下载。Loader 必须匹配包散列及包内当前架构成员，不能只因它出现在安装目录就信任。旧 `baseline-a4fc` 的 legacy result/install-manifest 不属于 v2 收据；上面的基线示例须先具备真实匹配的 v2 构建收据，不能直接套用旧路径或手工改写 schema。

先冻结并核对 MiniBlink DLL 所在完整依赖目录。WebView2 可使用调用方指定的 Fixed Runtime，或记录实际检测到的已安装 Runtime 版本。所有结果、报告、独占 WebView2 profile 均进入调用方指定的**全新**任务目录，不写应用资源或旧结果。

```powershell
& ../perf-host-baseline/Release/SouiSdkPerformanceHost.exe `
  --backend=miniblink --page-root="$((Resolve-Path benchmarks/sdk-performance-host/resources).Path)" `
  --result-dir="$([IO.Path]::GetFullPath('../perf-results-baseline-mb-r1'))" `
  --miniblink="$((Resolve-Path ../baseline-runtime/node.dll).Path)" --engine-budget=0
# WebView2 同一程序：--backend=webview2，省略 --miniblink。
# 可选 --fixed-webview2=<绝对目录>；默认只使用已安装 Runtime，不下载。
```

窗口显示使用 `SW_SHOWNOACTIVATE`；工具不调用 `SetForegroundWindow`、`SetActiveWindow`、`SendInput` 或 UIA。操作者自行切换、点击记录、输入和滚动。同一对比必须使用相同页面 hash、场景、负载、viewport、DPI、显示器和有效记录条件；跨后端/构建不得仅比较一个即时读数。

“重新加载当前页”在原动作许可被领取的 UI owner 轮次调用 `reload()`；只有精确的 `E_RELOAD_PENDING` 才保留同一次操作，在后续 housekeeping 机会继续取结果。首次成功后停止续调；新文档 ready 与新 session 仍须另行观察。消费者新增从首次动作开始计算的固定20秒合作式期限，在调用前后检查，不能抢占单次调用，也不重置原外层运行期限；其他错误和超时仍失败并正常收尾。关闭会取消后续续调，在当前 reload 回调退栈前保留 Host/adapter。`action-completed` 仅表示原操作返回成功，不代表页面已经重连或性能验收通过。旧消费者曾把 pending 当失败关闭窗口，其失败证据保持原身份。

`--engine-budget=0|1000|2000` 只在当前进程首次 Runtime 构造前设置，不能运行中切换。先以两边 0 对比 SDK；I02 三档仅重启同一候选应用比较。CMake 数值 package version 与 `<soui/version.h>` 的 raw 发行身份分别写入报告，例如候选 `0.7.0` / `0.7.0-preview.1`，不能与基线 `0.6.0` 冒充同一内容。

`--observation=off|scalar` 默认 off，必须在配对记录中保持一致；当前进程记录影响调度的环境项。消费者不配置 detailed trace，不允许 `SOUI_ENTRY_FILE` 覆盖冻结页面。窗口客户区、DPI、实际已加载内核路径/散列或 WebView2 runtime/process 身份随报告保存。内存计数明确只有 native host 进程，不冒充完整 renderer process tree。

环境身份包含 W02 实际读取的 `SOUI_WEBVIEW2_PREPARE_OUTPUT`、`SOUI_WEBVIEW2_OWNER_BUDGET_US`、`SOUI_WEBVIEW2_OUTPUT_TIMING`；未设置为 null，超过读取上限明确标记不可取得。普通配对必须三项相同；W02 控制实验需明确唯一变量，不能把继承环境变化混成 SDK 收益。

### 原生 Bridge 分段诊断（ADR-0159）

scalar Begin/End 另附 `clockAnchor`：用 QPC 前后括号夹住一次 UTC FILETIME 读取，记录实际 QPC 频率、读取跨度和采用的 UTC API。优先动态探测精确 API，旧系统回退 `GetSystemTimeAsFileTime`，不提高最低系统要求。UTC 精度未知、可发生墙钟调整；该字段不校准 renderer 的 `performance.now()`，不改变原 scalar 来源资格。采集器必须先 ready，再开始页面区间；仅在 Native Begin 后才启动采集的记录只能算部分覆盖，不能宣称完整 30 秒。

消费者同时记录冷环境项 `SOUI_DIAGNOSTIC_BRIDGE_PHASES`。仅在明确的诊断载荷中将它设为 `1`，保持 `SOUI_DIAGNOSTIC_SCRIPT` 未设置，并启用 `--input-trace=scalar`；必须在进程启动前冻结这些值。本组统一使用未设置来隔离 SCRIPT 诊断；已核对的 Host 仅在值为 `1` 时开启，`0` 会关闭，仍拒绝实际意外出现的 script 后缀。普通入口不设置该开关。这个独立观测有额外QPC/原子成本，不是默认性能配置，也不代表既有scalar计时全部关闭。

若实际诊断文本出现 `,script=` 后缀，即使冻结环境声明SCRIPT=0，也会以 `unexpected-script-observation` 将新phase资格标记为false：该后缀表示实际经过额外诊断脚本路径。原始文本与wire的旧script后缀仍完整保留，不能据初始环境掩盖运行时矛盾。

`slot::publish_snapshot` 仍只利用原UI每秒缓存机会。它从现有 `application_diagnostics()` 提取不超过8KiB的固定 `native_phases` JSON，保留原始字符串一次，另附不含桶的小 `nativePhaseCache` 摘要。`benchmark.identity` 返回wire副本时移除新增扩展并保持旧诊断前缀/script后缀，避免页面反复保存完整3×64桶。关闭开关且没有扩展时，旧snapshot字段不变。Bridge worker只复制缓存，不调用Host getter或等待UI。

原生Begin/End的 `report-N.json` 在 `sourceIdentity.nativeHost.applicationDiagnostics` 保留原始扩展；新增边界元数据记录实际phase captureQPC、publicationSequence、observerIncarnation及相对原生boundary开始的cacheAge。年龄只在同QPC频率、非未来时钟、有效源与相同session下计算。原SDK/kernel的 `snapshotUsable/sourceStatus` 和最终31stage/15metric NDJSON保持原义，私有phase缺失/故障不能借用它们的资格或补零。

离线模块 `benchmarks/protocol/native-bridge-phases.cjs` 的 `analyzeBoundaries(begin,end,{maximumCacheAgeMs:2000})` 接受两份已解析boundary对象，重新解析原文并逐字段核对缓存摘要、age和身份。uint64运算使用BigInt；固定三个阶段/64桶，缺源、fault、不同mount/进程/session、回退、同一缓存重复取数、超限age均给独立未合格结果。相同sequence但实际capture更新且计数未变可报告零完成样本，分位为null。桶分位只给inclusive QPC上界，末桶上界UINT64_MAX；平均值保留sum/completed精确分数。

该模块只验证boundary内容，调用方仍须核冻结输入、原生写盘saved回执、完整载荷、自然退出与隔离证据。原生文件限128KiB、页面报告60000字节和完整Bridge消息64KiB不放宽。新phase不等于DOM精确30秒窗口：跨边界in-flight和同步嵌套跨度原样保存，不能相减分位、叠加为CPU占比或解释成逐输入延迟/物理呈现。私有desktop即使hasFocus=true也不产生物理性能资格。

2026-11-02 的 `candidate-native-phases-sdk-r1` x64 Release 诊断消费者已在 LLVM22/MSVC142/Windows SDK 19041 下构建，并通过35项无窗口原生合同（含新增3组消费者合同）。只执行了一轮私有非输入desktop的Bridge 4KiB／30秒载荷，自然退出、Job0，无强制清理。原SDK/kernel scalar与三个phase的实际保存数据分别合格；phase缓存capture区间为29.5353482秒，reply阶段结束时仍有1个in-flight。首次离线读取器误把静态 `report.identity` 当作带 `native` 的Web身份而退出1，失败原件保留；修正为检查 `pageReport.identity.native` 后仅重读同一批文件，没有追加GUI。这不是v143、物理输入/呈现、性能提升或生产验收。

新增无窗口reader契约：

```powershell
node --test tests/benchmark/input-frame-trace/native-bridge-phases.test.cjs
```

原生 `--self-test` 也包含实际consumer helper的缓存/投影/时钟/大小边界合同；源码和Node合同通过不代替新SDK/consumer编译或真实诊断运行。页面资源和原30秒/固定工作负载未修改，不为此重复运行页面性能测试。

`--input-trace=off|scalar`（launcher 对应 `-InputTrace`）默认 off，与上面的 runtime metrics 是两个不同开关。scalar 在任何 SDK Host/Bridge 构造前冷配置公共 process recorder 和 MiniBlink trace source，不在热运行中 attach。页面仅在此档调用 `measurementBegin/End`：Begin 回执后启动 DOM 30 秒，结束时立刻提交 End，并以 `reportStatus` 确认两份快照实际保存。scalar flood 使用 7 个槽，留 1 个给 End；off 使用 8 个槽且完全没有 Begin/End 请求或额外原生 trace QPC/snapshot，比较必须同档。

End实际transport发送先于报告生成/预览更新，sentDomMs在编码后紧邻send的onAttempt记录；写盘状态的回执到达时也必须满足共同10秒期限。原生回执的snapshotUsable与sourceStatus区分“文件已保存”和“请求来源可用且观测flags无损有效”：缺源/饱和/时钟/丢失等原件仍保存，但不能成为有效对照。此资格不是完整因果、原子快照或quiescent证明。

原生 `soui.sdk-performance-scalar-boundary/v1` 文件包含 SDK/内核的 31 stage、16 counter（含 copy bytes/operations）和 15×64 duration bucket（含 input_queue/provider/FIFO/serialization），uint64/QPC 均为十进制字符串。每对绑定 Host/session 代际和唯一 measurement id；计数是全进程累积，包含其他 Host 和采样 Bridge 工作，live snapshot 跨计数器非原子，不能当作单 Host 因果链。每份快照单列 captureBegin/End QPC 和频率；Bridge 排队会使原生边界与 DOM 窗口错开，不能跨域相减或冒称物理输入延迟。无内核能力写 unavailable/null，保留 missing/saturation/clock/drop 状态，不能变成 0。所有 Host/Bridge 和实际 Runtime 自然退出后才 detach/stop，最后导出标准 `scalar-sdk-final.ndjson`、可用时的 `scalar-kernel-final.ndjson` 和 `scalar-final-status.json`。

每份 boundary 文件和入队回执都包含 `snapshotUsable` 与 `sourceStatus.sdk/kernel`：`requested`、`available`、`complete` 和 `reasons`。不适用来源的 complete 为 null；请求来源缺失、身份/模式不符、丢失、饱和、时钟或 owner 等故障令 snapshotUsable=false，原件仍可保存。complete 只表示该次读取观察到的 scalar 来源资格，不代表静止、原子性、完整因果链或物理性能；scalar 正常的 attempted 与 recorded 不等不当作丢失。页面只有 DOM 有效、两份文件确认保存、两份资格均可用才标记可对照；IO complete 与 measurementUsable 分开。最终 export 成功也与最终来源资格分开。

`--frame-rate=30..1000` 默认 60，只是原生请求目标；有效 pacer 还受当前显示器和候选后端反馈上限约束。高刷对照两边指定相同目标，报告同时保留 requested、pacer 与 SDK diagnostics，不能把 pacer Hz 叫作 FPS。Canvas 模拟始终固定 60 step/s；窗口移动/显示模式事件才重新查询显示器上限，不逐帧查询。

已冻结手动包可使用同目录 `Launch-Manual.ps1`：

```powershell
powershell.exe -File ./Launch-Manual.ps1 -PairManifest ./pair.json -Variant baseline -Backend miniblink -EngineBudget 0 -FrameRate 60
powershell.exe -File ./Launch-Manual.ps1 -PairManifest ./pair.json -Variant candidate -Backend webview2 -EngineBudget 0 -FrameRate 60
```

launcher 的 `soui.sdk-performance-manual-pair/v1` manifest 包含相对路径 `pageRoot`、`pageFiles` 和 `variants.baseline/candidate`，各 variant 包含 `executable`、`miniblink`、`runtimeFiles`；每个文件有 path/bytes/sha256。启动前逐个核验所选运行依赖与公共页面，路径不得逃出包。启动/退出侧车和每轮新 GUID 目录在包内 results，任何已有输出都不覆盖。

需要完整进程树CPU/内存时，两边同样追加 `-SampleProcessTree`（默认关闭）。包内 `processTreeSampler` 单独绑定已有测量工具的hash；启动后按应用PID和creation FILETIME附加只读观测，200ms间隔、最多10分钟，保留各样本的complete和实际run.end。它不注入输入，不计入被测应用的子进程树。应用退出后等待观测器退休；缺失/不完整/过期或强制退休的观测器使launcher非零，不能当成完整采样。开启观测有额外成本，只比较同开关/工具/间隔的轮次。手动关闭应用前先保存所有页面报告；不要将进程全程汇总直接当作某一个30秒DOM窗口。

## 生命周期与保存

- Bridge 复用 `json_bridge` 的 bootstrap/session/origin/token 校验和原有异步 native transport，不加入 CDP、DevTools、文件选择器或其他权限。Echo 只接受固定边界内的 ASCII payload 和 uint32 sequence。
- 页面 report 最多 60 KiB，保存到原生生成的唯一文件名；页面不能选择路径。独立有界 writer 返回 admission 后再提供 `reportStatus`，只有实际写入并 flush 成功才显示 saved。普通页面的保存仍由操作者触发，不混入记录内热路径。
- 仅冻结的 W02 页面可在 `<html>` 上设置 `data-soui-autobench="bridge4096-v1"` 以启用一次性自动驱动。它选择现有 `bridge` 场景和 4096 字节负载；只在 WebView2、`inputTrace=scalar`、`observation=scalar`、`SOUI_WEBVIEW2_OUTPUT_TIMING=1`、`SOUI_WEBVIEW2_PREPARE_OUTPUT=0|1` 配置且文档可见、窗口前台时调用原有 `begin(true)`。窗口未前台时等待真实 focus/visibility 事件；运行中失焦/隐藏继续按原规则使报告无效。不会重试或更改期限。
- 自动页只在 30 秒 DOM 窗口有效且 Begin/End 两份原生快照均实际保存并可用后调用原 `saveReport` 一次；只有 `reportStatus` 返回 `saved` 才经已有代际回执检查的 `benchmark.action` 队列请求 `close`。Host 将此私有 action 转成所属窗口的 `WM_CLOSE`，继续走原有 Host/Runtime 正常关闭路径。marker 缺失、配置错误、窗口失焦/隐藏、报告无效或写盘失败都不会自动关闭，也不会伪称报告已保存。该 marker 和 action 不属于 SDK 公共 API。
- 原生 action 仅进入有界 UI owner 队列，绑定 handle_json 入口冻结的代际和 SDK contextual request 原始 session。可嵌套 TLS scope 绑定各 Bridge，不再读取处理过程中新代际给旧请求续期。发布与 shutdown 代际推进共用短 gate；UI 执行前 claim 同一 receipt。撤销开始先将入口 epoch 原子置零，实际 inner shutdown 成功且全部并发/嵌套撤销结束后才重新开放；期间请求永久持有无效 receipt，异常或计数溢出保持 fail-closed。claim 是执行线性化点，后续撤销不重贴身份；gate 不跨 Host/COM/shutdown_session 调用。measurement 的 snapshot/大 JSON 编码在 gate 外，只有有界内存 writer 接纳与 active id 发布在 gate 内。第二 Host 最多一个，使用独立 Bridge/profile；reload 撤销原会话；hide-restore 隐藏 1.5 秒后以不激活方式恢复，期间记录必须 invalid。
- 关闭保留 adapter、Bridge、HWND 和 WebView2 exit watch 到其正常结束；MiniBlink 所有 Host 关闭后等待公共全局 runtime shutdown。writer 先停止接纳，再从 UI 非阻塞等待退出。期限失败保留失败并以非零 fail-stop，不把强制结束记成通过。

`identity.json`、`lifecycle.ndjson` 和最终 `exit.json` 记录原生身份/退出；页面报告包装身份、native snapshot、页面数据。消费者不把 scalar 支持等同于 detailed trace，也不启用 DevTools。真实运行和手动 30 秒数据未执行时，构建/算法测试结果不代表性能提升。
