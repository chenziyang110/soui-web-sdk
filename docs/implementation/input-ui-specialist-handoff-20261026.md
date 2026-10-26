# 输入到可见反馈：专人一次性交接

日期：2026-10-26。本文交接源码、fixture 和下一次执行顺序。实施阶段没有构建、执行测试、运行 benchmark/profile 或启动 VM；下列命令全部未执行，空白结果不能解释为通过。基线为 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`（Draft PR110，依赖链仍须 Root 核对）。**最终集成 candidate head 尚待 Root 冻结**；任何 worker commit、本文 commit 或旧预编译 DLL 都不能代替它。

Root 冻结后向验证人员填写唯一交接单：baseline/candidate 的完整 commit 与依赖、各自匹配的 SDK 公共头/LIB/内核 DLL/Host EXE、量测源码 commit、页面/工具/配置/载荷散列、source manifest/补丁、工具链/架构、物理目标环境和输出目录。C++ 布局变化必须匹配重编，不能把新 Host 头和旧 core LIB 混用。Root 最终逐项核对 worker fixture 的 **16 个 live source / 15 个复制输入**映射，以及新增 trace `.cpp` 的单次实际编译闭包；此数量是待核对口径，不是来源门槛已经通过的证据。

## 需求、源码、fixture、证据与状态

下表的“源码交付”只描述当前实现/设计差异。所有行的构建、fixture 执行、真实 Host 正确性和性能证据均为**未运行**。全局注册、来源及 WKE glue 以 Root 冻结树为准；缺少任何必需项先修复交接，不用旧用例/旧二进制补位。

| 工作包 | 已交付源码/行为入口（相对仓库根） | 实际 fixture 或场景入口 | 需要返回的证据 / 当前状态 |
|---|---|---|---|
| W00 | `content/browser/SharedTimerWin.h`、`content/web_impl_win/WebThreadImpl.*` / `WebTimerBase.*`；`cc/trees/LayerTreeHost.*` 的单个 legacy deadline Task、dirty 合并和失效票号。前三条 vendor 路径均位于 `third_party/sources/miniblink/`；PR110 同线程计时起点不计收益 | `tests/native/render_deadline_policy/`、`scheduler_continuation/`、`vendor_lifetime/worker_lifetime/` | 提前唤醒/投递失败/取消/关闭/重建；默认 DIRECT_HWND 和 legacy 分别证明。代码待编译，不能称已复现或已消除默认路径卡顿 |
| W01 | SDK `diagnostics/input_frame_trace`、`miniblink_trace_source`、私有 backend validator；vendor `content/common/InputFrameTrace.*`；`wke/InputUiApi.cpp` 的版本化数值表；A/B 各 owner 的实际测点 | `tests/native/input_frame_trace/recorder_tests.cpp`、`input_frame_trace_backend/`、`tests/benchmark/input-frame-trace/`；真实 `benchmarks/input-ui-host/` 和 `benchmarks/protocol/` 新 burst/reader/runner | 身份/频率/尺寸、slot owner/QPC、OOM、溢出、并发快照、producer quiescence、缺失记录/后端失败和完整 end；一次 trace 开关扰动预检。源码待编译/运行，T3b 和物理显示未知继续未知 |
| W02 | `sdk/src/host/{engine_admission.h,engine_input_service.h,input_sequence.h,interaction_protocol.cpp,soui_miniblink_host.cpp,engine_thread.cpp}`；typed admission、独立 edge/text 额度、取消/physical-release quarantine、多 Host 轮转、resize 接纳先于 UI publication | `tests/native/input_admission/{fixture.cpp,ordinal_cases.h,terminal_cases.h,RESIZE-FAILURE-SCENARIOS.md}`、既有 `interaction_router_tests.cpp`；实际 Host 的 `tests/native/input_frame_trace/INPUT-CANCEL-SCENARIOS.md` 与 `cancel-page/` | 满额/OOM/关闭/FIFO、单调 sequence、IME、关键 release、公平进展；真正 overload 取消不能产生 click/commit，释放后新输入可用；实际 resize 拒绝不改变几何且下一次 click 可达。policy fixture 不能证明 Blink DOM 行为 |
| W03 | vendor `SharedTimerWin.h`、`WebThreadImpl.*`、`WebTimerBase.*`；ready/future/idle、实际单调 deadline、合并 owner continuation、停止/关闭失败 fencing 和 completed Task retirement | `tests/native/scheduler_continuation/`；worker lifetime 的主/worker scheduler、close 和 retirement case | 不早跑/漏醒、早 timer、rearm/cancel、压力恢复、PostMessage/close/unregister 失败；真实退出前 Task 必须实际析构。正常 idle 不应新增固定周期轮询；兼容 fallback 分列 |
| W04 | vendor `content/web_impl_win/TaskSchedulingIndex.h`、`TaskSchedulingPolicy.h`、`WebThreadImpl.*` / `WebTimerBase.*` | `tests/native/scheduler_index/`、`scheduler_fairness_tests.cpp`；continuation/worker admission budget case | 随机操作与原策略 oracle、同键明确 tie-break、50ms aging、取消/重复/self-delete；future 多/ready 少的选择、promotion 与接纳成本和后台进展。代码待验证，不声称无界维护已由 fixture 自动排除 |
| W05 | SDK `frame_rate_policy.h` / `frame_pacer.h`；vendor `FrameDeadlinePolicy.h`、`WebPageImpl.*`、`LayerTreeHost.*`；商/余数与分数相位、迟帧跳过、同帧身份/deadline | `fractional_frame_phase_tests.cpp`、既有 `frame_pacer_tests.cpp`；真实 hide/restore、resize/DPI/跨屏 | 各刷新率固定时钟、首 dirty、迟帧、真实呈现间隔与 wake/CPU。属于必需源码改进，不能误列为默认关闭候选；配置 Hz 不等于实测 FPS |
| W06 | vendor `ResourceNotificationPolicy.h`、`ActionsFrameGroup.cpp`、`RasterTask.cpp`、`LayerChangeAction.cpp`、`LayerTreeHost.*` / `WebPageImpl.*`；注册等待后重查、合并资源通知、取消 receipt、保留 active scene | `resource_notification_policy_tests.cpp`、`render_scene_transaction_tests.cpp`、既有 `tests/native/scroll_coverage/` | 完成在注册前/中/后、重复/迟到/永不 ready、分配/投递失败、取消 scene 与 action 区间；不把取消标 ready。正常 1ms/8ms 轮询移除，异常一次 100ms 重试与保留 intent 分开记录 |
| W07 | vendor `FrameBufferLeasePolicy.h`、`PaintLifetime.h`、`LayerTreeHost.*`、`WebPageImpl.*`、`wkeWebView.*` / `wkeWebWindow.cpp` / `CursorDescriptorBroker.cpp` 和 Root `wke.cpp`；两槽 lease、镜像消费者契约、锁外客户端调用、延后关闭 | `frame_buffer_lease_policy_tests.cpp`；实际 Host 的 callback 关闭/嵌套 pump、持 BitInfo foreign release、DC/截图、alpha/resize/DPI/关闭场景 | 像素等价、两槽/256MiB pool、mirror 当前快照、最后 lease 的 buffer deleter/notification/post 完成后才解 fence、Task 实际析构和真实 join；active/pending/queued/callouts 不能假 drained。GPU 实验分支没有继承软件资格 |
| W08 | vendor `RasterSchedulingPolicy.h`、`RasterCandidateBudget.h`、`RasterTask.cpp`、`CompositingLayer.cpp` / `TileGrid.cpp` / `LayerTreeHost.*`；准入、visible/prefetch 顺序、取消恢复基础设施 | `raster_scheduling_policy_tests.cpp`、`render_scene_transaction_tests.cpp`；真实低核/多核、迟 tile/反向/边界/关闭 | 默认 1 worker，`SOUI_RASTER_WORKERS=2..4` 仅显式请求；最多 64 task/64MiB 受控候选、128 layer/1024 tile 检查。**opaque SkPicture 引用驻留上界未证明**，`UnknownPictureOwner` 在任何候选 dispatch 前回退 worker0。真实非空资格和采用均未关闭；无 profile 不是不采用证据 |
| W09 | 已有 layout/record/scene/copy/锁/callback、engine 各类服务及 serialization 的 W01 指标契约；B `SceneCoordinator` 所有权移动。A shared turn budget 候选及集成身份须在最终冻结核对 | scene transaction、既有输入/调度公平 fixture；真实 DOM/JS、Bridge 与后台工作 profile | layout/几何缓存、immutable list 等仍待热点证据。`SOUI_ENGINE_SHARED_TURN_BUDGET_US=100..5000` 默认 0/off；只作为待数据候选，保留每类至少一次进展及原 count 上限。指标存在不等于已经量得热点 |
| W10 | `sdk/renderers/webview2/webview2_host.cpp`、owner 输出表示/预算候选；不修改外部 Runtime 内部调度。A 最终 candidate/fixture 交接仍须 Root 核对 | 既有 `webview2_request_capacity_tests.cpp`、`webview2_prepare_tests.cpp`、`webview2_preparation_lifetime_tests.cpp`、`webview2_capacity_host/`；新 owner policy fixture 见下节冻结项 | `SOUI_WEBVIEW2_OWNER_BUDGET_US=100..5000` 默认 0/off，`SOUI_WEBVIEW2_PREPARE_OUTPUT=1` 默认 off；输出保序、COM owner、容量/来源/代际/导航 revoke/late result/close、真实 Runtime 版本。待数据决定采用，无性能结果 |
| W11 | [ADR-0126](../adr/0126-gpu-input-ui-candidate.md)、[GPU gate](input-ui-gpu-gate-20261026.md) | 仅具体设计/诊断与后续故障/采用矩阵 | 默认不启用，待软件 profile；当前实验 GPU callback 锁/设备恢复/关闭资格仍缺，本轮未实现合格硬件后端 |
| W12 | 本文、Root ADR/CHANGELOG、严格注册集和 staged source delta/manifest | 专人按下述顺序执行并回传原件 | 最终 head、来源/fixture closure 和 Draft PR 仍由 Root 冻结；本文不授予 performanceGate/releaseReady |

## 原生 fixture 的准确注册闭包

`V` 表示 `third_party/sources/miniblink`。以下是 Root 最终严格 registry 的核对清单；“建议”名称尚未全局注册，不得把表中名字当成已经可运行。必须在实际 `ctest --show-only=json-v1` 中检查名称、命令、架构和来源完全匹配，不放宽已有 exact-set gate。Release 下的显式失败返回逻辑须保留。

| 实际源码/leaf | CMake executable target | CTest 名称 | 必要实际编译输入/注册注意 |
|---|---|---|---|
| `input_admission/fixture.cpp`（含 `ordinal_cases.h`、`terminal_cases.h`） | `soui_input_mailbox_tests` | `sdk.native.input-mailbox` | leaf 已定义；链接本次匹配的 `Soui::Sdk`，其 `input_sequence.h`/mailbox/engine 实现不能来自旧 LIB。详细 recorder/router/mailbox 的 copy rejection、handler exception、queued close、explicit cancel 需唯一终态 |
| `input_frame_trace/recorder_tests.cpp` | `soui_input_frame_trace_tests` | `kernel.native.input-frame-trace` | 编译 SDK `sdk/src/runtime/diagnostics/input_frame_trace.cpp` 与 **V`/content/common/InputFrameTrace.cpp` 各一次**；include SDK/V；WINVER/`_WIN32_WINNT=0x0601`、`NOMINMAX`；vendor tests ON |
| `input_frame_trace_backend/validation_tests.cpp` | `soui_input_frame_trace_backend_tests` | `sdk.native.input-frame-backend-validation` | SDK public trace/POD header 与私有 `input_frame_trace_backend_validation.h`；当前 header-only validator fixture 不加载真实 DLL，不能代替实际 FFI 测试 |
| `render_deadline_policy/` → `../render_deadline_policy_tests.cpp` | `soui_render_deadline_policy_tests` | **`kernel.native.legacy-paint-deadline`** | 沿用现有 leaf 名称，不能重复注册另一个 render-deadline-policy；实际 `LegacyPaintRetryPolicy.h`，不另链接 vendor `.cpp` |
| `scheduler_continuation/fixture.cpp` | `scheduler_continuation_contract` | `scheduler.continuation.contract`、`scheduler.deadline.close-failure`、`scheduler.deadline.unregister-failure` | leaf generator 提取实际 `SharedTimerWin.h`、`TaskSchedulingPolicy.h`、`TaskSchedulingIndex.h` 和 `WebThreadImpl.cpp` 方法；保留 source charset/stubs、失败子进程包装和全局接入 |
| `scheduler_index/fixture.cpp` | `scheduler_index_contract` | `scheduler.index.contract` | 实际 `TaskSchedulingIndex.h` / `TaskSchedulingPolicy.h`；不得复刻被测算法或悄悄退回旧 vector 实现 |
| `scheduler_fairness_tests.cpp`（既有修改入口） | `soui_scheduler_fairness_tests` | `kernel.native.scheduler-fairness` | 实际 vendor header；原策略 oracle 与新 index tie-break/aging 分开证明 |
| `vendor_lifetime/worker_lifetime/fixture.cpp` | `soui_worker_lifetime_regressions`；source-byte target `soui_worker_lifetime_source_bytes` | 现有 23 个 `kernel.native.worker-lifetime.<case>` 名称保持；特别核对 `worker-scheduler-no-count-cap`、`worker-scheduler-clock-budget`、各 `main-scheduler-*` 和 `registry-live-fence` | 由 `source-map.json` / `source-bytes.cmake` 对实际原始编码 bytes 生成；Root 最终核对 16 live/15 copies 与实际 trace `.cpp` 单次编译。保留 stubs、宏、`user32`/`winmm`、fail-fast 包装；不改成旧 opaque LIB |
| `fractional_frame_phase_tests.cpp` | 建议 `soui_fractional_frame_phase_tests` | 建议 `kernel.native.fractional-frame-phase` | SDK `frame_rate_policy.h` + V`/cc/trees/FrameDeadlinePolicy.h`，均为实际 header；两个 include root，无 vendor `.cpp` |
| `resource_notification_policy_tests.cpp` | 建议 `soui_resource_notification_policy_tests` | 建议 `kernel.native.resource-notification-policy` | 实际 V`/cc/trees/ResourceNotificationPolicy.h`，header-only |
| `frame_buffer_lease_policy_tests.cpp` | 建议 `soui_frame_buffer_lease_policy_tests` | 建议 `kernel.native.frame-buffer-lease-policy` | 实际 V`/cc/trees/FrameBufferLeasePolicy.h` 与 `content/common/PaintLifetime.h`，header-only + C++ thread；包含 activity/cancel/foreign-release/mirror 源码案例，仍不是完整 GDI/Host 生命周期执行 |
| `input_frame_coverage_tests.cpp` | 建议 `soui_input_frame_coverage_tests` | 建议 `kernel.native.input-frame-coverage` | 实际 V`/cc/trees/{InputFrameCoverage.h,ViewTraceIdentity.h}` 及 trace POD header；helper fixture 无需另编 trace `.cpp` |
| `raster_scheduling_policy_tests.cpp` | 建议 `soui_raster_scheduling_policy_tests` | 建议 `kernel.native.raster-scheduling-policy` | 实际 V`/cc/raster/{RasterSchedulingPolicy.h,RasterCandidateBudget.h}`，header-only；不能由合成 budget 成功宣称真实 SkPicture 资格通过 |
| `render_scene_transaction_tests.cpp` | 建议 `soui_render_scene_transaction_tests` | 建议 `kernel.native.render-scene-transaction` | **实际 V`/cc/input/ScrollController.cpp` 和 `cc/trees/ScrollTreeSnapshot.cpp`**，因为 ScrollRect/ScrollNodeKey 等构造不是 inline；参考现有 scroll leaf 的 include/stubs/宏/CRT，不能只编 header 或链接旧 lib |

W10 新 `owner_output_policy` fixture 的实际路径、target/CTest、commit 尚待 A 交接及 Root 冻结，本表不虚构名字。没有完成该来源/注册项时，不能把新候选的必要 regression-source 交付记为关闭。

完整产品还必须实际编译 A/B 接入的 `WebThreadImpl.cpp` / `WebTimerBase.cpp` / `WebPage.cpp` / `WebPageImpl.cpp`、`LayerTreeHost.cpp` / `ActionsFrameGroup.cpp` / `RasterTask.cpp` / `LayerChangeAction.cpp` / `ScrollTreeSnapshot.cpp` / `CompositingLayer.cpp` / `TileGrid.cpp`，以及 `wkeWebView.cpp` / `wkeWebWindow.cpp` / `CursorDescriptorBroker.cpp` / Root `wke.cpp` / `InputUiApi.cpp` 和 vendor trace module。policy target 不需要链接这些大单元，却也不能证明这些单元已编译或接线正确。

`SouiInputUiHost`、`BurstInput`、`OwnedProcessRunner`、`ProcessTreeSampler`、`DisplayCalibration` 是实际量测/监督 executable target，**不是新增的 CTest 通过项**。前者由根 `SOUI_BUILD_INPUT_UI_BENCHMARKS=ON` 显式加入，或通过其 standalone CMake 使用匹配安装的 SDK；`BurstInput` 必须编译 `burst-input.cpp` 和 `burst-desktop-observer.cpp`。取消页面和 resize 失败命名场景也不是自动 CTest；要保留真实执行步骤、native receipt 与 DOM oracle。

## 一次执行的顺序与未运行命令

先冻结，再做一次工具预检；通过后每架构一次相关正确性集合；然后 W01 集中 profile 和代表性配对。失败先定位，只重跑受变更影响部分。以下 PowerShell 是专人模板，路径/工具链由冻结单提供，不能直接把占位值视作本次证据；**本文作者未运行任何一条**。

```powershell
# 从冻结源码根执行；保留本轮命令、stdout/stderr、退出码和产物身份。
$inputUiRoot = '<冻结源码根绝对路径>'
$inputUiArch = 'x64' # 关键正确性随后以 x86 再执行一次；不是双架构性能笛卡尔积。
$inputUiConfig = 'Release'
$inputUiCtest = '<冻结 ctest.exe 绝对路径>'
$inputUiBuild = Join-Path $inputUiRoot "out/$inputUiArch"

& ./tools/Doctor.ps1 -Architecture $inputUiArch
& ./tools/Build-Dependencies.ps1 -Architecture $inputUiArch -Component All
# 先按冻结的依赖来源/部署契约绑定本次重建 DLL/LIB，再构建应用；禁止跑旧 prebuilt。
& ./tools/Build.ps1 -Architecture $inputUiArch -Configuration $inputUiConfig -WithVendorTests -WithWebView2 -FreshConfigure
node tools/internal/vendor-tests.cjs inspect $inputUiRoot $inputUiArch $inputUiConfig $inputUiCtest true
& $inputUiCtest --test-dir $inputUiBuild -C $inputUiConfig --show-only=json-v1
```

这些已有接口示例使用默认 MSVC143。若冻结为 LLVM22MSVC142，必须使用现有 Doctor/Build/Build-Dependencies 的六个显式外部路径、依赖额外 `VsInstallDir`/全新 `OutputRoot` 和构建/owned/profile request 绑定；`vendor-tests inspect` 也传 `--toolchain-request <实际请求>`。不能省略参数后把默认 MSVC 构建报告当 LLVM 结果。源树预检/manifest、SDK 安装和应用链接必须保存原有严格契约；Root 更新新增头、`.cpp`、source-map 与 exact test names 后才可执行。

Root 冻结实际 registry 后把下面选择表达式与实际注册名逐项比对，再运行相关集合一次；不匹配或返回零项就是交接失败。若使用 `Test.ps1 -RequireVendorTests` 完成相同集合及严格门槛，不再重复下面同样的 CTest；不使用 `-NativeOnly` 绕过 vendor/unit 门槛。

```powershell
$inputUiSelection = '^(sdk\.native\.(input-mailbox|input-frame-backend-validation|interaction-router|frame-pacer|source-kernel-api|webview2-capacity|webview2-prepare|webview2-preparation-lifetime)|kernel\.native\.(input-frame-trace|legacy-paint-deadline|scheduler-fairness|fractional-frame-phase|resource-notification-policy|frame-buffer-lease-policy|input-frame-coverage|raster-scheduling-policy|render-scene-transaction|scroll-coverage|worker-lifetime\..*)|scheduler\.(continuation\.contract|index\.contract|deadline\.(close|unregister)-failure))$'
& $inputUiCtest --test-dir $inputUiBuild -C $inputUiConfig -R $inputUiSelection --output-on-failure
node --test tests/benchmark/input-frame-trace/api-mirror.test.cjs tests/benchmark/input-frame-trace/protocol.test.cjs tests/benchmark/input-frame-trace/series.test.cjs
# 原生工具 preflight 需显式绑定实际本轮工具，不能跳过后记为通过。
$env:BENCHMARK_BURST_INPUT_EXE = '<本轮 BurstInput.exe 绝对路径>'
$env:BENCHMARK_OWNED_RUNNER_EXE = '<本轮 OwnedProcessRunner.exe 绝对路径>'
node --test tests/benchmark/input-frame-trace/native-preflight.test.cjs
```

若 continuation/index/legacy leaf 尚未纳入根 registry，可以按 leaf 的实际 CMake 单独 configure/build/CTest，但必须在交接单记录独立 build root、source gate 和结果，不能把不存在的全局项算作执行。Root 优先完成全局精确登记；新增 W10 case 在冻结后加入相关选择集。其他既有 scroll barrier/CSS Home/coverage/ACK 用例按实际影响集运行，不为凑数量重跑无变化的全套冷/热启动。

真实 x86/x64 正确性补充：实际 IME（Unicode SendInput 不能替代）、drag/selection/key-char/focus/capture 与 Ctrl+wheel、两 Host 洪流公平、512 ordinary slot 满时 resize 拒绝与恢复、导航/重载/关闭积压；default DIRECT_HWND 与 legacy；永不 ready/取消/迟到资源；透明/alpha、DC/截图需求、resize/DPI；paint callback 重入/嵌套 pump、最后 foreign lease 释放、拒投/分配失败、真正 Task 销毁和最终 Runtime join。保留 accepted/rejected/recovery 及 DOM/pixel 原件，不以无崩溃替代状态守恒。

QEMU/隔离 guest 只用于功能、故障注入和关闭验证；noVNC 图像、虚拟时钟和虚拟刷新率不能证明真实高刷、T7 尾延迟或 T8 物理发光。Win7/PE 兼容分支单列，不能用现代 DXGI 测量替代。

## 工具预检和 W01 profile 的交接点

一次工具预检先校准同机 QPC/桌面 collector、held PID/创建身份、焦点/窗口命中/内容区、原件散列与自然 Owned 关闭，执行无目标、错身份、session/SendInput/collector 失败路径并确认非零退出。只有用匹配内容像素的 marker 同次提交证明观察有效；tag、handler 计数或 ACK 跳号都不是单独的输入/可见性证明。

详细 trace 显式冷配置、每进程一轮、实际线程冷注册；默认 scalar。分别确认 SDK/vendor 真能力，不以 `--trace=full` 字符串证明。exporter 独占 consumer/stream，每源最多 100000 events/<64MiB，stop 非阻塞；实际 producer 释放、排空和 recorded/exported 一致后才可写完整 end。丢记录、overflow、causal loss 或后端 unavailable 不能取得完整因果资格。geometry-only 改变时非空间输入仍可功能投递，但 received identity 不改；当前跨 geometry native 关联显式 causal loss，不能按时间近似补链。

在同一候选、同一负载上做 trace 关/开配对扰动预检：详细 trace 额外 CPU 拟定不超过一逻辑核容量的 2%，T1→T6 P95 扰动不超过 `max(0.2ms, 关闭时5%)`；首次预检冻结噪声和阈值。关闭状态缺可比较的真实 T1→T6 观测时，该扰动门槛仍未闭，不能从 scalar 桶制造逐输入延迟。超限或噪声不可分时 full 只诊断，正式收益使用已证明低干扰的对称 E2E 路径。

相关正确性通过后，专人一次集中 profile 回传：engine native/provider/input/FIFO、task queue/选择/接纳、layout/record/scene、资源完成→激活、copy 次数/bytes、raster bytes/驻留、锁 wait/hold、client callback、Bridge serialization/broadcast、WebView2 owner 输出；同时记录 queue oldest/latest age、wake/s、CPU、峰值内存/句柄和后台吞吐。并行 CPU 时间不当串行墙钟，各阶段 P95 不相加。profiling 不能中途改默认开关或架构再把数字拼成同一轮。

给 W08 返回真实 picture/资源所有权和可见 ready 成本；给 W09/W10 返回可归因热点及候选收益/公平性；给 W11 返回软件 raster/copy/提交等适合 GPU 的关键路径证据。缺数据一律“待数据”，不能把“没有热点证据”写成已有不采用结论。W08 opaque owner 证明、W09/W10 采用、W11 硬件实现/资格仍是开放工作。

## 代表性配对、停止与返回格式

新协议的真实入口与请求字段见 [INPUT-FRAME-TRACE.md](../../benchmarks/protocol/INPUT-FRAME-TRACE.md) 和 [公共 Host README](../../benchmarks/input-ui-host/README.md)。先按匹配 ABI 重编 `SouiInputUiHost`、协议工具和产品；旧基线若无诊断 API，只允许明确 E2E-only。诊断 overlay 仅复制六个中立诊断文件，保持旧 baseline Host/public layout 头和旧 core LIB 匹配；不能把 overlay 当作旧内核已有新 hook。

```powershell
node benchmarks/protocol/run-input-ui.cjs '<冻结 request JSON 绝对路径>' '<新的结果目录绝对路径>'
node benchmarks/protocol/input-frame-trace.cjs '<trace-sdk.ndjson>' '<trace-vendor.ndjson>'
node benchmarks/protocol/same-product-input-pair.cjs '<baseline结果目录>' '<candidate结果目录>'
node benchmarks/protocol/same-product-input-series.cjs '<系列结果根目录>'
```

正式收益两端使用相同 E2E 模式，candidate full 另列诊断，不能以不对称详细 trace 配对。先选一台隔离物理目标的 60Hz 和一个真实高刷模式、相关 default/legacy、pointer/wheel/Bridge/后台工作场景；WebView2 按真实 Runtime 单列。125/500/1000Hz 使用 paced waitable timer burst，8kHz 仅有真实硬件时另扩。轮次每边不超过 100000 注入/120s，Host 活动 150s，关闭/Runtime/trace 收口共享总 20s；不逐阶段延长。full 轮次还受每源 100000 trace point 上限，不能靠无限落盘凑样本。

新 burst 每冻结场景至少五个有效交替配对、candidate 累计至少 10000 条可判定终态输入；可见/合并/取消/stale/not-visible/rejected/unknown 守恒，OS/coalescing 与 instrumentation unknown 分开。实际 stage/T7 交集样本单列，coalesced 不新增 T7 样本，不能拿 10000 注入数作 P99 分母。保留逐轮事件分布/P95/P99/最大值和不确定性；不得对五轮平均值取 P99。当前 DKW 输出是以 IID 为条件的名义 rank band，burst 独立性未证实，不能宣称已验证 95% 置信区间。稀疏点击不足样本时标探索性，报告门槛未满足，不无限延长。

旧 serial-ACK 协议保持 S01–S04 每轮约 61s、S05 约 123s，真实 runner 窗口不变；它不能冒充 1000Hz 连续输入，新短 burst 也不能冒充旧时长。注入 T0 是 SendInput 前/后区间；T1/T2/T6 仅实际测点；T3b 未建立相关性继续 unavailable；桌面 captured-image T7 带校准/遗漏误差，物理 T8 未测。缺 T3b 不妨碍独立证实的 T1→T6/T7 样本，却不能使 complete-all-stage-chain 变真。

每次启动前遵守 series `nextAction`：相同环境原因连续两次 invalid 停该场景；一次关键输入/像素/fence/关闭/资源守恒产品失败停该候选并保留原件，先关闭/回退相关候选再定位。日志丢失、身份/QPC 不符、意外遮挡、锁屏/RDP/桌面切换/拓扑变化、duplication 失败、marker 不可见或外部负载等不能平均进有效结果。计划内 hide/遮挡仅是功能段，不补零 T7；恢复后重建有效观察。禁止无限自动重试、STOP 改自然退出或强删 fence/close 检查。

最终返回一个冻结身份索引和原始证据目录：构建命令/输出/退出码、实际 registry/source-byte 检查、每个相关 CTest/真实场景结果、trace 扰动门槛、每轮有效/无效原因和计数、实际阶段/E2E 分布及采样数、资源/后台进展、剩余 unknown、候选开关和回退。报告分别标“代码完成待编译 / 编译通过待实测 / 正确性通过待性能 / 限定场景通过 / 未通过”，不得只给平均 FPS、PR 数量或绿色总灯。Root 按发现修复并只重跑受影响集合，最终集成候选一次有界核心复验；不合并、不发布。
