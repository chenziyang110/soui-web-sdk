# 输入到可见反馈：专人一次性交接

日期：2026-10-26。本阶段已交付生产源码、必要回归SOURCE、独立静态审查、ADR、严格来源和分批Draft PR。未配置/编译、未执行C++ SOURCE/CTest、应用、真实COM/GL、benchmark/profile或VM；下列执行命令均未运行。只做源码/差异/语法/字节与来源清单元数据核对；只读内存提取未落地测试产物。

最终集成候选为本独立GPU/交接Draft的分支 `codex/input-ui-gpu-handoff-20261026`，以该PR实际head及执行前`git rev-parse HEAD`一致为准。正文不以worker提交或前序快照替代最终head。基线是PR110的`44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`；PR109、尚未实测的PR110和最终候选必须分别冻结，不能把异条件数字拼为收益曲线。

| Draft依赖 | 范围 | 已发布源码head |
|---|---|---|
| [110](https://github.com/chenziyang110/soui-web-sdk/pull/110) | 基线/ready continuation | `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed` |
| [111](https://github.com/chenziyang110/soui-web-sdk/pull/111) | W00 | `ca58a1b0f5e2e97e6df84051bdc1cb9954a956f4` |
| [116](https://github.com/chenziyang110/soui-web-sdk/pull/116) | W01基础/协议 | `8718ccb30c3e14539e16c374217aa172b967523b` |
| [117](https://github.com/chenziyang110/soui-web-sdk/pull/117) | W02接纳 | `d558e363ec84cecfb1c46d022f4ea9a91e5c9b79` |
| [118](https://github.com/chenziyang110/soui-web-sdk/pull/118) | W03/W04 | `5bbfe4d4c9acd4e64461516921ae493e1f49d82f` |
| [119](https://github.com/chenziyang110/soui-web-sdk/pull/119) | W05/W06 | `b0690ea9f13d7b8f86eb25d126269ef6f3c7e696` |
| [120](https://github.com/chenziyang110/soui-web-sdk/pull/120) | W07/场景/关闭与W08守卫 | `ec530a40f8612058a31603b0687bc4142f1231c7` |
| [121](https://github.com/chenziyang110/soui-web-sdk/pull/121) | SDK调用点/冷租约/W09 | `af3f94054343774ea33efa5eaace1baffdbdd89b` |
| [122](https://github.com/chenziyang110/soui-web-sdk/pull/122) | W10 | `4ed101d6118900fdd048b905edbd6e54506cf66a` |

本分支只增加W11设计/诊断候选和W12交接，没有启用或实现新的合格GPU后端。根`source-manifest.json`、`third_party/manifests/kernel-input-ui-*-source.json`和raw patch0015–0023固定各包来源；固定prebuilt保持历史身份，releaseReady/performanceGate不据此变true。worker最终16 live/15 copies/22payload已逐字节核对，仅审过C trace两whole原件更新；原关闭proof保持。全局SOURCE静态登记为78个精确kernel名称+4个scheduler辅助名称，50项preflight，SDK新增leaf另外列于下表。这些数量不是运行通过数。

验证人员从独立干净checkout和独占输出根填写一次冻结单：最终完整commit、匹配重编的SDK公共头/LIB/内核DLL/Host EXE、量测源码、页面/工具/配置/载荷散列、清单/raw patch、工具链/架构、实际Runtime版本、物理目标环境、命令和产物身份。C++布局变化必须整体匹配，禁止新头/旧DLL混用。原dirty开发目录与其他任务构建不参与验证。

## 需求、源码、fixture、证据与状态

下表的“源码交付”只描述当前实现/设计差异。所有行的构建、fixture 执行、真实 Host 正确性和性能证据均为**未运行**。全局注册、来源及 WKE glue 以 Root 冻结树为准；缺少任何必需项先修复交接，不用旧用例/旧二进制补位。

| 工作包 | 已交付源码/行为入口（相对仓库根） | 实际 fixture 或场景入口 | 需要返回的证据 / 当前状态 |
|---|---|---|---|
| W00 | `content/browser/SharedTimerWin.h`、`content/web_impl_win/WebThreadImpl.*` / `WebTimerBase.*`；`cc/trees/LayerTreeHost.*` 的单个 legacy deadline Task、dirty 合并和失效票号。前三条 vendor 路径均位于 `third_party/sources/miniblink/`；PR110 同线程计时起点不计收益 | `tests/native/render_deadline_policy/`、`scheduler_continuation/`、`vendor_lifetime/worker_lifetime/` | 提前唤醒/投递失败/取消/关闭/重建；默认 DIRECT_HWND 和 legacy 分别证明。代码待编译，不能称已复现或已消除默认路径卡顿 |
| W01 | SDK `diagnostics/input_frame_trace`、`miniblink_trace_source`、私有 backend validator；vendor `content/common/InputFrameTrace.*`；`wke/InputUiApi.cpp` 的版本化数值表；A/B 各 owner 的实际测点 | `tests/native/input_frame_trace/recorder_tests.cpp`、`input_frame_trace_backend/`、`tests/benchmark/input-frame-trace/`；真实 `benchmarks/input-ui-host/` 和 `benchmarks/protocol/` 新 burst/reader/runner | 身份/频率/尺寸、slot owner/QPC、OOM、溢出、并发快照、producer quiescence、缺失记录/后端失败和完整 end；一次 trace 开关扰动预检。源码待编译/运行，T3b 和物理显示未知继续未知 |
| W02 | `sdk/src/host/{engine_admission.h,engine_input_service.h,input_sequence.h,interaction_protocol.cpp,soui_miniblink_host.cpp,engine_thread.cpp}`；typed admission、独立 edge/text 额度、取消/physical-release quarantine、多 Host 轮转、resize 接纳先于 UI publication | `tests/native/input_admission/{fixture.cpp,ordinal_cases.h,terminal_cases.h,RESIZE-FAILURE-SCENARIOS.md}`、既有 `interaction_router_tests.cpp`；实际 Host 的 `tests/native/input_frame_trace/INPUT-CANCEL-SCENARIOS.md` 与 `cancel-page/` | 满额/OOM/关闭/FIFO、单调 sequence、IME、关键 release、公平进展；真正 overload 取消不能产生 click/commit，释放后新输入可用；实际 resize 拒绝不改变几何且下一次 click 可达。policy fixture 不能证明 Blink DOM 行为 |
| W03 | vendor `SharedTimerWin.h`、`WebThreadImpl.*`、`WebTimerBase.*`；ready/future/idle、实际单调 deadline、合并 owner continuation、停止/关闭失败 fencing 和 completed Task retirement | `tests/native/scheduler_continuation/`；worker lifetime 的主/worker scheduler、close 和 retirement case | 不早跑/漏醒、早 timer、rearm/cancel、压力恢复、PostMessage/close/unregister 失败；真实退出前 Task 必须实际析构。正常 idle 不应新增固定周期轮询；兼容 fallback 分列 |
| W04 | vendor `content/web_impl_win/TaskSchedulingIndex.h`、`TaskSchedulingPolicy.h`、`WebThreadImpl.*` / `WebTimerBase.*` | `tests/native/scheduler_index/`、`scheduler_fairness_tests.cpp`；continuation/worker admission budget case | 随机操作与原策略 oracle、同键明确 tie-break、50ms aging、取消/重复/self-delete；future 多/ready 少的选择、promotion 与接纳成本和后台进展。代码待验证，不声称无界维护已由 fixture 自动排除 |
| W05 | SDK `frame_rate_policy.h` / `frame_pacer.h` / `host_visibility_policy.h`；vendor `FrameDeadlinePolicy.h`、`WebPageImpl.*`、`LayerTreeHost.*`；商/余数与分数相位、迟帧跳过、同帧身份/deadline，以及实际 native visibility 发布链 | `fractional_frame_phase_tests.cpp`、既有 `frame_pacer_tests.cpp`、`input_admission/visibility_cases.h`；实际 hide/restore、resize/DPI/跨屏矩阵见下节 | 先核对实际 SDK show/pump→能力检查→owner setter→WebPageImpl/LTH，不能只证明 unused setVisible helper。各刷新率固定时钟、首 dirty、迟帧、真实呈现间隔与 wake/CPU；属于必需源码改进，配置 Hz 不等于实测 FPS |
| W06 | vendor `ResourceNotificationPolicy.h`、`ActionsFrameGroup.cpp`、`RasterTask.cpp`、`LayerChangeAction.cpp`、`LayerTreeHost.*` / `WebPageImpl.*`；注册等待后重查、合并资源通知、取消 receipt、保留 active scene | `resource_notification_policy_tests.cpp`、`render_scene_transaction_tests.cpp`、既有 `tests/native/scroll_coverage/` | 完成在注册前/中/后、重复/迟到/永不 ready、分配/投递失败、取消 scene 与 action 区间；不把取消标 ready。正常 1ms/8ms 轮询移除，异常一次 100ms 重试与保留 intent 分开记录 |
| W07 | vendor `FrameBufferLeasePolicy.h`、`PaintLifetime.h`、`LayerTreeHost.*`、`WebPageImpl.*`、`wkeWebView.*` / `wkeWebWindow.cpp` / `CursorDescriptorBroker.cpp` 和 Root `wke.cpp`；两槽 lease、镜像消费者契约、锁外客户端调用、延后关闭 | `frame_buffer_lease_policy_tests.cpp`；实际 Host 的 callback 关闭/嵌套 pump、持 BitInfo foreign release、DC/截图、alpha/resize/DPI/关闭场景 | 像素等价、两槽/256MiB pool、mirror 当前快照、最后 lease 的 buffer deleter/notification/post 完成后才解 fence、Task 实际析构和真实 join；active/pending/queued/callouts/native-failure 不能假 drained。GPU 实验分支没有继承软件资格 |
| W08 | vendor `RasterSchedulingPolicy.h`、`RasterCandidateBudget.h`、`RasterTask.cpp`、`CompositingLayer.cpp` / `TileGrid.cpp` / `LayerTreeHost.*`；准入、visible/prefetch 顺序、取消恢复基础设施 | `raster_scheduling_policy_tests.cpp`、`render_scene_transaction_tests.cpp`；真实低核/多核、迟 tile/反向/边界/关闭 | 默认 1 worker，`SOUI_RASTER_WORKERS=2..4` 仅显式请求；最多 64 task/64MiB 受控候选、128 layer/1024 tile 检查。**opaque SkPicture 引用驻留上界未证明**，`UnknownPictureOwner` 在任何候选 dispatch 前回退 worker0。真实非空资格和采用均未关闭；无 profile 不是不采用证据 |
| W09 | 已有 layout/record/scene/copy/锁/callback、engine 各类服务及 serialization 的 W01 指标契约；B `SceneCoordinator` 所有权移动；A `cooperative_turn_budget.h`、统一数值 Bridge trace、const input dispatch | scene transaction、`input_admission/fixture.cpp` 的预算 helper、`wake_cases.h`、既有输入/调度公平 fixture；真实 DOM/JS、Bridge 与后台工作 profile | layout/几何缓存、immutable list 等仍待热点证据。`SOUI_ENGINE_SHARED_TURN_BUDGET_US=100..5000` 默认 0/off；native/FIFO 截止受总预算限制，provider/input 仍保留一次有界批次，细粒度跨 provider 剩余预算协议待数据。内部自定义 wake bit 已撤去，各次 pending post 使用原 auto-reset event；指标/helper 不等于实际进展或热点已测 |
| W10 | `sdk/renderers/webview2/webview2_host.cpp`、`owner_output_policy.h` / `owner_callout_policy.h`；owner 输出表示/预算候选，实际 COM target pin、opaque getter 前后身份复验和 nested pump guard；不修改外部 Runtime 内部调度 | `tests/native/webview2_owner_policy/`，以及既有 `webview2_request_capacity_tests.cpp`、`webview2_prepare_tests.cpp`、`webview2_preparation_lifetime_tests.cpp`、`webview2_capacity_host/` | `SOUI_WEBVIEW2_OWNER_BUDGET_US=100..5000` 默认 0/off，`SOUI_WEBVIEW2_PREPARE_OUTPUT=1` 默认 off；同 item 的 raw+prepared UTF-16 合计不超过原 64KiB credit，整个活跃 pump entry 最多16项。worker 源码 `7d9ca9c0` / `7bc5ebfb` 已静态复核，含诊断 getter 关闭/导航重入及第二批递归排除的修复；不是最终集成 head，未实际 COM 执行或测性能 |
| W11 | [ADR-0126](../adr/0126-gpu-input-ui-candidate.md)、[GPU gate](input-ui-gpu-gate-20261026.md) | 仅具体设计/诊断与后续故障/采用矩阵 | 默认不启用，待软件 profile；当前实验 GPU callback 锁/设备恢复/关闭资格仍缺，本轮未实现合格硬件后端 |
| W12 | 本文、Root ADR/CHANGELOG、严格注册集和 staged source delta/manifest | 专人按下述顺序执行并回传原件 | 最终 head、来源/fixture closure 和 Draft PR 仍由 Root 冻结；本文不授予 performanceGate/releaseReady |

## 原生 fixture 的准确注册闭包

`V` 表示 `third_party/sources/miniblink`。以下名称已在最终根CMake/精确清单作源码登记与静态核对，仍须由专人查询实际配置生成的registry；静态登记不等于目标已构建或已运行。必须在实际 `ctest --show-only=json-v1` 中检查名称、命令、架构和来源完全匹配，不放宽已有 exact-set gate。Release 下的显式失败返回逻辑须保留。

| 实际源码/leaf | CMake executable target | CTest 名称 | 必要实际编译输入/注册注意 |
|---|---|---|---|
| `input_admission/fixture.cpp`（含 ordinal/terminal/wake/applied-identity/visibility cases） | `soui_input_mailbox_tests` | `sdk.native.input-mailbox` | leaf 已定义；链接本次匹配的 `Soui::Sdk`，其 `input_sequence.h`/mailbox/engine 实现不能来自旧 LIB。详细 recorder/router/mailbox 的 copy rejection、handler exception、queued close、explicit cancel 与重入 close 需唯一终态；晚到空 wake 不能阻断下一次输入，visibility helper 不代替真实 HWND 路径 |
| `input_frame_trace/recorder_tests.cpp` | `soui_input_frame_trace_tests` | `kernel.native.input-frame-trace` | 编译 SDK `sdk/src/runtime/diagnostics/input_frame_trace.cpp` 与 **V`/content/common/InputFrameTrace.cpp` 各一次**；include SDK/V；WINVER/`_WIN32_WINNT=0x0601`、`NOMINMAX`；vendor tests ON |
| `input_frame_trace_backend/validation_tests.cpp` | `soui_input_frame_trace_backend_tests` | `sdk.native.input-frame-backend-validation` | SDK public trace/POD header 与私有 `input_frame_trace_backend_validation.h`；当前 header-only validator fixture 不加载真实 DLL，不能代替实际 FFI 测试 |
| `render_deadline_policy/` → `../render_deadline_policy_tests.cpp` | `soui_render_deadline_policy_tests` | **`kernel.native.legacy-paint-deadline`** | 沿用现有 leaf 名称，不能重复注册另一个 render-deadline-policy；实际 `LegacyPaintRetryPolicy.h`，不另链接 vendor `.cpp` |
| `scheduler_continuation/fixture.cpp` | `scheduler_continuation_contract` | `scheduler.continuation.contract`、`scheduler.deadline.close-failure`、`scheduler.deadline.unregister-failure` | leaf generator 提取实际 `SharedTimerWin.h`、`TaskSchedulingPolicy.h`、`TaskSchedulingIndex.h` 和 `WebThreadImpl.cpp` 方法；保留 source charset/stubs、失败子进程包装和全局接入 |
| `scheduler_index/fixture.cpp` | `scheduler_index_contract` | `scheduler.index.contract` | 实际 `TaskSchedulingIndex.h` / `TaskSchedulingPolicy.h`；不得复刻被测算法或悄悄退回旧 vector 实现 |
| `scheduler_fairness_tests.cpp`（既有修改入口） | `soui_scheduler_fairness_tests` | `kernel.native.scheduler-fairness` | 实际 vendor header；原策略 oracle 与新 index tie-break/aging 分开证明 |
| `vendor_lifetime/worker_lifetime/fixture.cpp` | `soui_worker_lifetime_regressions`；source-byte target `soui_worker_lifetime_source_bytes` | 现有 23 个 `kernel.native.worker-lifetime.<case>` 名称保持；特别核对 `worker-scheduler-no-count-cap`、`worker-scheduler-clock-budget`、各 `main-scheduler-*` 和 `registry-live-fence` | 由 `source-map.json` / `source-bytes.cmake` 对实际原始编码 bytes 生成；Root 最终核对 16 live/15 copies 与实际 trace `.cpp` 单次编译。保留 stubs、宏、`user32`/`winmm`、fail-fast 包装；不改成旧 opaque LIB |
| `fractional_frame_phase_tests.cpp` | `soui_fractional_frame_phase_tests` | `kernel.native.fractional-frame-phase` | SDK `frame_rate_policy.h` + V`/cc/trees/FrameDeadlinePolicy.h`，均为实际 header；两个 include root，无 vendor `.cpp` |
| `resource_notification_policy_tests.cpp` | `soui_resource_notification_policy_tests` | `kernel.native.resource-notification-policy` | 实际 V`/cc/trees/ResourceNotificationPolicy.h`，header-only |
| `frame_buffer_lease_policy_tests.cpp` | `soui_frame_buffer_lease_policy_tests` | `kernel.native.frame-buffer-lease-policy` | 实际 V`/cc/trees/FrameBufferLeasePolicy.h` 与 `content/common/PaintLifetime.h`，header-only + C++ thread；包含 activity/cancel/foreign-release/mirror 源码案例，仍不是完整 GDI/Host 生命周期执行 |
| `input_frame_coverage_tests.cpp` | `soui_input_frame_coverage_tests` | `kernel.native.input-frame-coverage` | 实际 V`/cc/trees/{InputFrameCoverage.h,ViewTraceIdentity.h}` 及 trace POD header；helper fixture 无需另编 trace `.cpp` |
| `raster_scheduling_policy_tests.cpp` | `soui_raster_scheduling_policy_tests` | `kernel.native.raster-scheduling-policy` | 实际 V`/cc/raster/{RasterSchedulingPolicy.h,RasterCandidateBudget.h}`，header-only；不能由合成 budget 成功宣称真实 SkPicture 资格通过 |
| `render_scene_transaction_tests.cpp` | `soui_render_scene_transaction_tests` | `kernel.native.render-scene-transaction` | **实际 V`/cc/input/ScrollController.cpp` 和 `cc/trees/ScrollTreeSnapshot.cpp`**，因为 ScrollRect/ScrollNodeKey 等构造不是 inline；参考现有 scroll leaf 的 include/stubs/宏/CRT，不能只编 header 或链接旧 lib |
| `window_close_lifetime/{fixture.cpp,generate.cjs}` | `soui_window_close_lifetime_source_tests`（根已登记） | `kernel.native.window-close-lifetime-source` | 实际 `wkeWebWindow.cpp` close helper/`_destroyWindow`/NCDESTROY、native show/visibility messages/resize，及 `wkeWebView.cpp` callback-once/preflight，并复制 `PaintLifetime.h`；3 sources→8 copies（7 exact ranges +1 whole header），从原5 copies增加3个可见性片段。保留 CP936 source bytes/实际 source-charset 配置。使用 deterministic OS/page/scheduler fakes，不创建 HWND，不证明真实 Win32 close/visibility 已通过 |
| `webview2_owner_policy/fixture.cpp` | `soui_webview2_owner_policy_tests` | `sdk.native.webview2-owner-policy` | leaf 已定义，根已登记，未执行；实际 `owner_output_policy.h` 与 `owner_callout_policy.h`，header/fake SOURCE 检查预算/配额/严格解析、getter 重入/target pin/new-document flags 与 nested guard；不加载 COM/真实 Runtime |
| `host_frame_policy/fixture.cpp` | `soui_host_frame_policy_tests` | `sdk.native.host-frame-policy` | leaf 已定义，根已登记，未执行；实际 SDK `host_frame_policy.h` 的 controller/CPU override/legacy/显式 main/GPU 选择及安全 ceil interval，纯 policy，无 SDK recorder/vendor/WebView2 链接依赖；不证明真实 cadence/显示效果 |
| `independent_input_trace_tests.cpp` | `soui_independent_input_trace_tests` | `kernel.native.independent-input-trace` | 实际 `IndependentInputTrace.h` / `InputFrameCoverage.h` / `ScrollTreeSnapshot.h`，并编译当前 `ScrollController.cpp`、`ScrollTreeSnapshot.cpp`；测试 consumed receipt 与 visual sample、zero visual、smooth 同 tick、generation/scene 权限及 overflow，不授予 visible/content-marker 标志 |
| `independent_wheel_source/{fixture.cpp,generate.cjs}` | `soui_independent_wheel_source_tests`（根已登记） | `kernel.native.independent-wheel-source` | 按 bytes/hash 复制/提取实际 payload、admission、draw wake/Task receipt 和 owner retry；18 个复制均为实际 UTF8 字节，使用冻结 CMake 的 `/utf-8`，并编译生成的实际 `cc/input/ScrollController.cpp` / `cc/trees/ScrollTreeSnapshot.cpp`。保留 generated include；receiving scheduler/render 是明确 dependency fakes，不创建 HWND/compositor |
| `host_visibility_source/{fixture.cpp,generate.cjs}` | `soui_host_visibility_source_tests`（根已登记） | `kernel.native.host-visibility-source` | worker `e769c980` 的实际 `HostVisibilityPolicy.h` / restore Task / owner / main frame / AutoRecord / LTH setVisible；6 原始 source →10 copies（4 whole headers、5 WebPageImpl fragments、1 LTH fragment），实际字节为 UTF8（含 AutoRecord 非 ASCII），使用 Root 冻结 CMake 的 `/utf-8`；仅生成 include + fixture，不另链接 vendor `.cpp`。SOURCE 验证失败/OOM/隐藏保留 dirty 与 close 后 queued disposal/late run；不创建 HWND。恢复只强制 composite full damage，并保留已有 legacy UI dirty，不新增 DIRECT_HWND mirror copy |
| `render_diagnostic_source/{fixture.cpp,generate.cjs}` | `soui_render_diagnostic_source_tests`（根已登记） | `kernel.native.render-diagnostic-source` | worker `5e27085d` 的实际 `InputFrameTrace.h` / `RenderDiagnosticTrace.h` / `GpuSkiaSurface::present`，3 sources→3 copies，fixture + generated includes，不另链接 vendor `.cpp`。GL/recorder 是显式 fakes；检查 SwapBuffers/count/frameDirty 行为与 stage29/30 零关系固定码，不证明实际 GPU 资源/呈现资格，也不执行 GL |
| `native_trace_ui_lease/fixture.cpp` | `soui_native_trace_ui_lease_tests` | `sdk.native.native-trace-ui-lease` | leaf 已定义，根已登记，未执行；编译实际 SDK `native_trace_ui_lease.h`，链接匹配 `Soui::Sdk` scalar diagnostics，module/API 是显式 fakes。覆盖共线程多 Host/不同 getter/module、active-call pin 跨最后 unmount、pin/retain 失败、foreign 最终析构保留引用并记 loss；不加载 DLL，不证明真实 producer 注册/退出已通过 |

以上leaf的最终global源码登记/来源已核对；专人的实际registry若缺项或多项应失败，不能算零项通过。独立 wheel 的 queued-close 计数修复已在 worker `f95efc75` 静态复核：实际 Task disposal 退休共享 obligation，不能直接清零假 drain。上述源码修复不构成已运行证据。

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
$inputUiPowerShell = '<冻结 PowerShell 可执行文件绝对路径>'
$inputUiBuild = Join-Path $inputUiRoot "out/$inputUiArch"
$inputUiDependencyOutput = '<本轮独占的依赖输出根>'

node tools/internal/source-archive.cjs verify
& ./tools/Doctor.ps1 -Architecture $inputUiArch
& ./tools/Build-Dependencies.ps1 -Architecture $inputUiArch -Component Miniblink -OutputRoot $inputUiDependencyOutput
# 先按冻结的依赖来源/部署契约绑定本次重建 DLL/LIB，再构建应用；禁止跑旧 prebuilt。
& ./tools/Build.ps1 -Architecture $inputUiArch -Configuration $inputUiConfig -WithVendorTests -WithWebView2 -FreshConfigure
node tools/internal/vendor-tests.cjs inspect $inputUiRoot $inputUiArch $inputUiConfig $inputUiCtest true
& $inputUiCtest --test-dir $inputUiBuild -C $inputUiConfig --show-only=json-v1
```

这些已有接口示例使用默认 MSVC143。若冻结为 LLVM22MSVC142，必须使用现有 Doctor/Build/Build-Dependencies 的六个显式外部路径、依赖额外 `VsInstallDir`/全新 `OutputRoot` 和构建/owned/profile request 绑定；`vendor-tests inspect` 也传 `--toolchain-request <实际请求>`。不能省略参数后把默认 MSVC 构建报告当 LLVM 结果。源树预检/manifest、SDK 安装和应用链接必须保存原有严格契约；Root 更新新增头、`.cpp`、source-map 与 exact test names 后才可执行。

Root 冻结实际 registry 后把下面选择表达式与实际注册名逐项比对，再运行相关集合一次；不匹配或返回零项就是交接失败。若使用 `Test.ps1 -RequireVendorTests` 完成相同集合及严格门槛，不再重复下面同样的 CTest；不使用 `-NativeOnly` 绕过 vendor/unit 门槛。

```powershell
$inputUiSelection = '^(sdk\.native\.(input-mailbox|input-frame-backend-validation|interaction-router|frame-pacer|host-frame-policy|source-kernel-api|engine-startup-rollback|webview2-capacity|webview2-prepare|webview2-preparation-lifetime|webview2-owner-policy|native-trace-ui-lease)|kernel\.native\.(input-frame-trace|legacy-paint-deadline|scheduler-fairness|fractional-frame-phase|resource-notification-policy|frame-buffer-lease-policy|input-frame-coverage|raster-scheduling-policy|render-scene-transaction|independent-input-trace|independent-wheel-source|window-close-lifetime-source|host-visibility-source|render-diagnostic-source|scroll-coverage|lifetime\.(endpoint|ctor_shell|dragdata)-.*|request-close\..*|worker-lifetime\..*)|scheduler\.(continuation\.contract|index\.contract|deadline\.(close|unregister)-failure))$'
& $inputUiCtest --test-dir $inputUiBuild -C $inputUiConfig -R $inputUiSelection --output-on-failure
& $inputUiPowerShell -NoProfile -File tests/sdk/llvm-generated-test-dependencies.Tests.ps1 `
  -CandidatePath (Join-Path $inputUiRoot 'tools/internal/Observe-LlvmBuild.ps1') -RecipeRoot $inputUiRoot
# 上条仅验证observer规则的authored graph反例，不是实际编译/Ninja证据。
node --test tests/benchmark/input-frame-trace/api-mirror.test.cjs tests/benchmark/input-frame-trace/protocol.test.cjs tests/benchmark/input-frame-trace/series.test.cjs
# 原生工具 preflight 需显式绑定实际本轮工具，不能跳过后记为通过。
$env:BENCHMARK_BURST_INPUT_EXE = '<本轮 BurstInput.exe 绝对路径>'
$env:BENCHMARK_OWNED_RUNNER_EXE = '<本轮 OwnedProcessRunner.exe 绝对路径>'
node --test tests/benchmark/input-frame-trace/native-preflight.test.cjs
```

continuation/index/legacy及新render SOURCE都已纳入根源码登记，先核对实际registry再运行一次；不另建重复leaf执行来补同一集合。若实际缺项，先修复配置/来源并记录失败，不能用不存在的全局项或旧fixture补位。其他既有 scroll barrier/CSS Home/coverage/ACK 用例按实际影响集运行，不为凑数量重跑无变化的全套冷/热启动。

真实 x86/x64 正确性补充：实际 IME（Unicode SendInput 不能替代）、drag/selection/key-char/focus/capture 与 Ctrl+wheel、两 Host 洪流公平、512 ordinary slot 满时 resize 拒绝与恢复、导航/重载/关闭积压；default DIRECT_HWND 与 legacy；永不 ready/取消/迟到资源；透明/alpha、DC/截图需求、resize/DPI；paint callback 重入/嵌套 pump、最后 foreign lease 释放、拒投/分配失败、真正 Task 销毁和最终 Runtime join。保留 accepted/rejected/recovery 及 DOM/pixel 原件，不以无崩溃替代状态守恒。

QEMU/隔离 guest 只用于功能、故障注入和关闭验证；noVNC 图像、虚拟时钟和虚拟刷新率不能证明真实高刷、T7 尾延迟或 T8 物理发光。Win7/PE 兼容分支单列，不能用现代 DXGI 测量替代。

## W05 / W07 / 独立 wheel 的实际执行矩阵

这是匹配重编产品和真实 Host 的专人交接，所有项均未运行。policy/fake SOURCE 通过后仍须补这些实际边界；每轮绑定窗口/输入/帧/代际与 held process、实际 source/二进制/页面。功能段不混入正式可见延迟分母，失败只重跑受影响集合。

| 场景 | 实际触发与必须保留的 oracle | 需要记录 / 不得推断 |
|---|---|---|
| W05 实际 hide/show、父窗口 minimize/restore | 公共 Host hide/show；另沿实际 legacy `wkeShowWindow`/CWebWindow `WM_SHOWWINDOW`/`WM_WINDOWPOSCHANGED`/`WM_SIZE` 路径最小化/恢复，含交错与 hide→show 在一次 owner 应用前完成。SDK 路径追到 UI sample→version/cap/size 检查→owner setter→WebPageImpl/LTH；native bridge 在 default style 处理后采样实际 attached HWND，不只调用 helper | setter 的 actual OK/unknown/pending、main/composite phase、dirty、hidden CPU/wake、恢复首个内容全量像素；最小化保留旧 viewport，旧附着 HWND/foreign owner 消息不得 resize 当前 view。覆盖 ShowWindow/DefWindowProc 同步关闭、external cap setter 改状态后 ack cache 失效、拒投后 retained fresh intent。public session hidden、setter OK 和 T7 是三种不同事实；不把有意 hidden 段补为零延迟 |
| W05 scene/resource 受阻时隐藏并恢复 | 保存尚未 ready 的 action/scene，隐藏期间正常保留工作与必要后台进展；恢复、资源真实完成、再提交；同尺寸恢复也覆盖 | dirty 并集、action/coverage/fence、旧帧不冒新帧、无补跑过期脉冲；没有真实完成通知不能宣称资源 ready。旧 DLL 无可见性能力时单列兼容路径与未资格项 |
| W05 DPI/跨屏/刷新率与迟帧 | 60Hz 加一个真实高刷；DPI/输出切换、相同像素尺寸但 scale 改变、resize、长 JS 后恢复。使用实际公共 resize/owner 路径 | device/buffer/geometry generation、分数相位与跳过迟帧、实际 frame interval/P95/P99/CPU/wake；配置 Hz、pacer pulse 或 noVNC FPS 均不等于呈现 |
| W07 default DIRECT_HWND / legacy / DC / 透明 | 同冻结内容分别运行默认无额外 mirror 消费者、legacy/nonindependent、首次及重复 DC/截图、透明/layered；显式 false 不能撤销 sticky DC/必要消费者 | 像素/alpha 等价、copies 与 bytes/frame、锁 wait/hold、client callback 时间、有效 mirror demand；借用 HDC 只保证获取时快照，不能跨 resize/paint 保存裸 DC |
| W07 持 lease、callback 重入与原生关闭失败 | 持两个实际读 lease/BitInfo，再 resize/close；最后 lease 在 foreign thread 释放；paint/cancel callback 请求 close、嵌套 pump；DestroyWindow 失败、FALSE after exact NC、旧 NC 与新 HWND 附着交错 | 两槽/字节上限、retained dirty、buffer deleter 与完成发布后才释放 activity；callback once→native preflight 成功→delete。unknown HWND/native failure 保留 owner 与 !drained；Task 真析构、实际 join、模块/句柄仍在用时不能强删 |
| independent wheel 的场景资格与回退 | 冻结有 eligible overflow node 的真实滚动页面；未知/旧 doc/geometry scene、pending resize/scene、handler/不支持区域、Ctrl+wheel、边界和反向；一次输入只允许一个 accepted 路径 | 原始 router sequence/generation 与单独 compositor receipt、实际 offset/coverage/fence/ACK；fallback 前 prediction 不变，accepted 后不重投 Engine 重复 delta。canvas wheel burst 的 DOM 反馈不能代替此资格 |
| independent wheel 的 publication / sample / paint | 分别在 payload/reserve 前失败、receipt 耗尽、发布后 draw Task OOM/拒绝/取消/延迟；zero/net-zero、smooth 同 tick、真实 sample 的 visual delta、buffer 受阻、scene 变化、 queued task 时关闭 | publication 前失败不动 prediction；publication 后保留输入/dirty，实际 task receipt 恰好退休并由既有有界 owner 机会重试。仅 consumed receipt 且真实 visual sample 应用于 layer/paint 才发对应 processed coverage，不能以 receipt 接纳冒充 visible |
| W01 独立 UI producer / 多次 submit | 同 UI 冷 native producer 租约、内部 loop 已注册时仅 borrow、多个 Host 共线程、最后 owner 退出、旧 ABI/失败；同 main/scene frame_id 多次独立 native submit | query/attach/detach、实际 SDK/vendor producer slot/lifetime、unregistered/dropped/causal loss及模块持有。`frame_id` 不是 native submit 唯一 ID；T6 保留 stream/producer/wire 事件与明确关联，跨 producer 同 tick 次序未知；没有关联就 unknown |

W05 native/SDK与native UI租约的最终capability/Root glue已静态接通，source identity见冻结单；静态helper及fake案例仍不构成实机通过。W10 在相同 Runtime/负载上另做 default、elapsed-only、prepared-only、combined 四组：核对事件/响应 JSON、raw+prepared/lane saturation、OOM 回退、导航 revoke/late response、真实 STA COM 重入/关闭、后台进展与序列化/owner QPC，按 profile 决定是否采用。

## 工具预检和 W01 profile 的交接点

一次工具预检先校准同机 QPC/桌面 collector、held PID/创建身份、焦点/窗口命中/内容区、原件散列与自然 Owned 关闭，执行无目标、错身份、session/SendInput/collector 失败路径并确认非零退出。只有用匹配内容像素的 marker 同次提交证明观察有效；tag、handler 计数或 ACK 跳号都不是单独的输入/可见性证明。

详细 trace 显式冷配置、每进程一轮、实际线程冷注册；默认 scalar。分别确认 SDK/vendor 真能力，不以 `--trace=full` 字符串证明。exporter 独占 consumer/stream，每源最多 100000 events/<64MiB，stop 非阻塞；实际 producer 释放、排空和 recorded/exported 一致后才可写完整 end。丢记录、overflow、causal loss 或后端 unavailable 不能取得完整因果资格。geometry-only 改变时非空间输入仍可功能投递，但 received identity 不改；当前跨 geometry native 关联显式 causal loss，不能按时间近似补链。

native UI producer 的 cap0x200 租约是显式冷入口，须在真实 UI 输入启用前检查 ABI/字段边界/cap/函数指针并固定实际 getter/retain/release 所属模块；热输入只持已有共享 token。已有内部 loop writer 时仅 borrow，必须保证该 writer 的内部生命周期包含整个借用期；无内部 writer 时才自注册，最后一个自有 lease 同线程 release 才 detach。多 Host 共线程不能提前 detach；foreign 析构保留引用并记 causal loss，不能称为 quiescent 成功。当前 detach 保留同 cookie 的暂停 TLS writer/storage 到线程退出；实际已初始化 SDK runtime 保留原 DLL 引用到进程生命周期，额外 lease pin 的归还不是通用 `FreeLibrary` 许可。真实 matched-DLL 的 UI→TaskPair.task_posted、module 保留、stop/producer exit/排空仍交由专人运行，当前 SOURCE 未执行。

在同一候选、同一负载上做 trace 关/开配对扰动预检：详细 trace 额外 CPU 拟定不超过一逻辑核容量的 2%，T1→T6 P95 扰动不超过 `max(0.2ms, 关闭时5%)`；首次预检冻结噪声和阈值。关闭状态缺可比较的真实 T1→T6 观测时，该扰动门槛仍未闭，不能从 scalar 桶制造逐输入延迟。超限或噪声不可分时 full 只诊断，正式收益使用已证明低干扰的对称 E2E 路径。

相关正确性通过后，专人一次集中 profile 回传：engine native/provider/input/FIFO、task queue/选择/接纳、layout/record/scene、资源完成→激活、copy 次数/bytes、raster bytes/驻留、锁 wait/hold、client callback、Bridge serialization/broadcast、WebView2 owner 输出；同时记录 queue oldest/latest age、wake/s、CPU、峰值内存/句柄和后台吞吐。并行 CPU 时间不当串行墙钟，各阶段 P95 不相加。profiling 不能中途改默认开关或架构再把数字拼成同一轮。

给 W08 返回真实 picture/资源所有权和可见 ready 成本；给 W09/W10 返回可归因热点及候选收益/公平性；给 W11 返回软件 raster/copy/提交等适合 GPU 的关键路径证据。缺数据一律“待数据”，不能把“没有热点证据”写成已有不采用结论。W08 opaque owner 证明、W09/W10 采用、W11 硬件实现/资格仍是开放工作。

## 代表性配对、停止与返回格式

新协议的真实入口与请求字段见 [INPUT-FRAME-TRACE.md](../../benchmarks/protocol/INPUT-FRAME-TRACE.md) 和 [公共 Host README](../../benchmarks/input-ui-host/README.md)。先按匹配 ABI 重编 `SouiInputUiHost`、协议工具和产品；旧基线若无诊断 API，只允许明确 E2E-only。诊断 overlay 仅复制六个中立诊断文件，保持旧 baseline Host/public layout 头和旧 core LIB 匹配；不能把 overlay 当作旧内核已有新 hook。

当前冻结的 burst canvas 页面没有 eligible overflow scroll node；其 wheel 场景证明 DOM 收到的数量/位移单位守恒及同内容反馈，**不证明 independent compositor scroll**。沿用实际 scroll offset/coverage/fence/ACK 的既有正确性集合，另由专人冻结确有 eligible scroll node、已绑定页面/轨迹/配置的滚动 workload 采 profile；不能从 canvas 成绩推广独立滚动尾延迟。独立路径只在真实 controller sample receipt 被消费且实际 visual delta 纳入对应帧时关联原始 input sequence/generation；不制造 engine_begin/T3，不用 compositor 自编号或时间接近代替收到时的输入身份。

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

## 静态来源闭包与保留的采用门槛

LLVM observer静态重绑真实writer，6个新角色中index只允许0生成；5个固定recipe共46个路径、8个已验证source/object关系，包含wheel两个generated.cpp。拒绝缺失/额外/错配unit、未知header、writer/pin篡改和build后source-binding.json授权。worker canonical LF hash与16/15/22实际map一致；实际compiler/Ninja/动态DLL证据仍须专人返回。新SDK-linked回归目标显式使用MD/U_DEBUG和15s超时。

W08尚缺真实非空SkPicture引用所有权/驻留字节的可证明资格域；当前默认单worker，未知owner在任何并发dispatch前回退。W09 layout/几何缓存、immutable list及更细provider剩余预算等待成本profile；W09/W10性能开关默认关闭。W11交付独立具体设计、诊断入口和故障/资源/采用矩阵，GPU device/fence/软件恢复/关闭实现及资格仍待数据和后续工作。以上仍是未关闭工作，不可称全部优化实现。T3b、未证明同内容提交的T7以及物理发光阶段保持未知。
