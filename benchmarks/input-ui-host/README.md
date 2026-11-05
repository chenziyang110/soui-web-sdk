# 实际公共 SDK Host 量测入口

`SouiInputUiHost` 使用真实 `soui_miniblink_host` / `dynamic_miniblink_adapter`、原生输入窗口、Bridge 和最终 Runtime 关闭。没有 raw WKE 页面/输入绕过。SDK 和内核 trace 在任何 Host/worker 注册前冷配置；详细模式才启动独立有界 consumer，不在 engine/UI 热路径落盘。

```powershell
<SouiInputUiHost.exe> --miniblink=<匹配DLL绝对路径> --page-root=<冻结protocol页面目录> --result-dir=<本轮已创建结果目录> --run=<32位小写hex> --kind=pointer_move --trace=full
```

输入器必须由 OwnedProcessRunner 持有，并使用它的真实 PID/创建 FILETIME；等待 `host-ready.json` 并与原生 start 复核，再启动 `BurstInput`。它仅接受 `SouiInputUiHostV1` 窗口，在 final ACK、桌面 collector 和输入原件持久化后设置绑定 run/PID/创建身份的本轮结束 event；不会向可能复用的 HWND 发送跨进程关闭消息。宿主收到后走真实 Host.destroy 和二十秒 Runtime 总期限，再 stop trace、等待 producer quiescent、排空和写完整结束；失败非零，不修改验收资格。

冻结的 burst HTML 原样复制为 page-bundle/index.html；run/kind 放在新建的有界 burst-config.js。公共 SDK 仍只信任 index/react 本地入口，不放宽 query/navigation 检查。指针用唯一坐标编码输入序列；滚轮固定 -120 单位，在 DOM 按实际 wheelDelta 累积，OS 聚合不改写为丢事件。全模式缺少对应能力即拒绝；`--trace=e2e` 在旧内核明确缺少 API 时可正常关闭，但缺失的内部阶段和 scalar 源仍标 unavailable。

consumer 每次最多1024项/源、4ms条件等待，两个64KiB缓冲文件均 CREATE_NEW。每 exporter 总计100000事件/小于64MiB；错误/超限/未静止不写完整 end。默认 SDK 不自动创建这个工具线程。窗口保留既有公共 benchmark 的16ms owner pump，不能将该工具的调度误称内核 deadline 优化收益。

活动期限固定150秒；Host.destroy、最终Runtime关闭和trace收口共享从关闭请求开始的20秒剩余预算，不逐阶段重新计时。完成字段只证明实际capability/getter与导出闭合；各阶段关联仍由真实point/content证据判定，未知继续unknown。

正式 `run-input-ui.cjs` 在输入器失败时保留非零结果，并允许 Host 在原 190 秒监督期限内完成既有收尾，避免立即强停截断 trace。若输入器未能发送结束 event，Host 仍受原活动/关闭期限约束；不会新计时或延长等待。Host 失败时停止仍存活的输入器，原 155 秒输入器监督与 195 秒总 watchdog 保持。完成通道拒绝、强清、停止请求错误或证据缺失均不能生成通过记录。针对编排的 mock 合同不冒充真实窗口或原生 Job 验收。更新测量源码后须重新生成新轮 v2 冻结请求，不修改旧请求的来源散列以冒充原版本。

旧基线需使用其**匹配的 SDK 库和公共 Host 头**。如旧安装缺新诊断模块，可在开始物理采样前运行 prepare-legacy-support.ps1，从冻结候选仅复制六个中立诊断文件到全新目录，再向本目录 CMake 传 SOUI_INPUT_UI_LEGACY_SUPPORT。该 include 树没有 Host/输入/渲染类头，不会把旧对象与新 C++ 布局混用。旧内核没有新表时只运行 e2e；overlay/source manifest、旧核心库、DLL、宿主 EXE 与同一冻结量测源码都必须进本轮来源/载荷清单。不能把 overlay 描述为旧核心已经获得新生产优化。

本目录 CMake 可独立 find_package，也可由集成根注册。初始源码交付时未运行编译或窗口测试；后续每轮必须分别记录编译、原生无目标/错误参数、真实窗口/关闭、overflow、trace 开关与实机性能的实际执行状态，不能从无窗口预检推断桌面通过。W02 的实际输入取消 oracle 与命名场景见 tests/native/input_frame_trace/INPUT-CANCEL-SCENARIOS.md。

## 正式路径与诊断路径

正式默认采用 `WS_OVERLAPPEDWINDOW`，`WM_SETFOCUS` 转发到公共 `host.focus()`，不额外调用 `SetForegroundWindow`/`SetActiveWindow`，不输出阶段日志。**原 BurstInput 默认仍调用 `ClientToScreen + SetCursorPos` 设置初始指针**，不能将旧 EXE 或无参数默认路径描述为无输入动作。客户区仍校正为固定 1100×720 物理像素。为保留旧 VM 排障方式，可分别显式传入 `--diagnostic-suppress-focus`、`--diagnostic-borderless`、`--diagnostic-foreground`、`--diagnostic-log`；重复开关或未知开关拒绝。任一开关都会将 ready/result 标记为 `diagnostic`。正式运行器会在启动输入器前拒绝它，结果 reader 同样拒绝，旧缺少路径标识的结果不能补写后重新验收。

`--trace=full` 采集详细 trace；`--trace=e2e` 不启用详细 trace consumer，但仍使用现有 scalar 模式，不是完全无 instrumentation。二者的测量税需单独评估，不修改 recorder ABI 来伪造 `off`。

`--diagnostic-log` 另保存固定生命周期边界 `scalar-diagnostic-*.json`：紧贴 Host start 前后、document ready 后且发布 ready 前、第一次 destroy 尝试前、destroy 成功后，以及 Runtime shutdown 成功后。每个阶段只读现有 scalar 一次，不轮询、不创建线程、不重设关闭期限。来源身份、QPC 包围值、原始 loss/registration 等字段保留；`before_destroy` 不表示 destroy 已完成，源仍运行时的计数不是原子跨线程快照。

这些额外快照始终属于诊断路径，不构成性能样本、有效窗口或来源完整性豁免。写入失败会记为 `diagnostic_scalar_boundaries_ok:false` 并最终非零退出，正常关闭仍继续，不通过窗口异常路径递归重试。原来的 scalar 初末导出、source_loss 拒绝及全生命周期范围保持不变；旧原件不补写。定位未注册写入时可用 `--manual-input --diagnostic-log` 避免初始指针定位，按新的冻结身份启动关闭，不将隔离桌面结果冒充物理输入测试。

## 手动输入与冷预算 profile

新增 `--manual-input` 才跳过上述指针定位，并以 `SW_SHOWNOACTIVATE` 显示窗口；正常焦点转发保持。它不能和 `--diagnostic-foreground` 组合。ready/result 明确记录 `input_mode:"manual"`、`deterministic_replay:false`；原默认记录 `burst-protocol/true`。手动模式没有输入器、校准或确定回放资格，不生成假的输入收据；新 profile 工具与旧 BurstInput runner/不同提交 pair 分开。

手动模式只加载独立 `manual-input-page.html/js`，将 HTML 原样复制为同名文件与 `index.html`，另生成 `manual-input-config.js`。原非手动路径仍复制原 burst 页与原 seed。新页接受任意方向/位置的 mousemove 和正负 wheel delta，实际更新本地事件计数/坐标与完整 1100×720 Canvas；只在输入或生命周期变化时请求一次待处理 rAF，空闲不循环绘制。local JS 计数不是 native input ID，不包含注入序号、CRC/T7 或确定回放标记。溢出、错误字段、失焦、隐藏、resize、DPR/尺寸不符均保留在只读 `souiManualInputResult()` 和画面状态中。此独立页面不能与旧 burst 页或固定 Bridge 页混合比较。

本轮手动操作结束后点击“完成记录”。页面只在用户点击后通过现有受授权 Bridge 发送本次冻结快照；没有输入期间的定时采样或额外 Bridge 流量。原生仅在 manual 模式、mount 前注册并授权 `benchmark.manualInputReport`；参数只含 `snapshot_json` 字符串，快照原文严格拒绝重复 key、未知字段、深层嵌套、错误类型和越界，参数与最终文件各不超过 8 KiB。外层 Bridge envelope 沿用既有 SDK 解析语义，不将此快照校验宣称为 SDK 原 wire 的重复 key 检查。

回执 `accepted:true,persisted:false` 只表示单次内存接纳；看到“已接纳，尚未落盘”后正常关窗。关闭接纳与提交共用短 mutex，锁不跨解析、JS、Host 或 IO；重复提交不覆盖首份，关闭后的迟到提交拒绝。正常 Host/Runtime 关闭且 producer 静止后，在原共享关闭预算内以 CREATE_NEW/完整写入/FlushFileBuffers 保存 `manual-input-result.json`，随后 host-result 的 `manual_report_submission`/`manual_report_persisted` 才记录实际终态。文件碰撞、写入、flush 或期限失败使进程非零；没有点击时文件明确写 `unsubmitted`、`page_snapshot:null`，host-result 的 persisted=false 不代表捏造零输入。页面回执不保证进程崩溃前已落盘。

报告的 run/PID/creation/QPC 由原生生成，不保存 session/token；页面自报 run 只作相等校验。reader 另核自然退出、原件散列、能力及身份。快照范围只到点击取数，`render_pending` 保留待处理绘制；后续输入和关闭仍进入全生命周期 scalar，不能把两组计数强行对齐。`counts.renders` 是 Canvas 回调数，unique frames、连续前台、等工作量和物理延迟仍 unknown/false。旧无 `manual-page-snapshot-v1` 能力的原件继续按历史 unknown 读取，不补快照；新 launcher 拒绝使用旧 EXE 执行此新流程。实现/Node 合同、C++ 编译和真实手动联调必须分别记录，不因源码已有此功能就声称已验证 GUI。

### 原生观察完成门

外部被动观察器运行时，控制器可显式追加 `--manual-observation-owner=PID:creation:nonce`；只允许 manual 模式，PID 与创建 FILETIME 为规范正整数，nonce 为非零 32 位小写 hex。不传时保留原接纳路径。该选项只属于 benchmark，不新增公共 SDK API 或产品线程。

Host 在自己的 HWND 创建后核对窗口线程/PID，再建立绑定 run、nonce、Host/controller PID/创建时间、HWND 的独立未置位 manual-reset event。事件 ACL 限定当前 logon SID，拒绝同名已存在对象；绝对过期点沿原 150 秒活动期限。名称与身份仅写原生 Ready，不进入页面配置。Bridge 授权之后、collector 接纳之前只作零超时状态检查；未结束返回 `submission:pending,retryable:true,accepted:false,persisted:false`，不消耗首次接纳机会。控制器死亡、期限失效或句柄错误返回终态错误。没有阻塞等待、轮询线程或新增页面计时器。

页面收到精确 pending 回执后丢弃本次快照并恢复按钮；下一次用户点击重新取数并使用新的请求 ID，不自动重发，旧回调不能完成新提交。观察结束不代表测量合格；接纳及正常关闭后的持久化仍须分别核对。gate/controller 句柄一直保留到 Runtime、trace 和回调静止，再检查关闭结果。

独立 `benchmarks/protocol/ManualObservationControl.cpp` 只提供查询当前控制器创建身份与发送结束信号两种显式模式。发送前持有并复核两个原始进程实例、Host HWND/class 和原绝对期限，只打开由绑定计算出的既有 event，置位一次并检查关闭；不创建、重置 event，不发送窗口消息或输入。页面没有设置该 event 的入口；同一 logon 下的恶意原生程序不在这个防误操作保证内。

从仓库根独立编译工具，不向原十工具图或 SDK 安装加入目标：

```powershell
cmake -S benchmarks/manual-observation-control -B out/manual-observation-control -G "Visual Studio 17 2022" -A x64 -T v143 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build out/manual-observation-control --config Release --target ManualObservationControl
```

这两条命令是支持工具链的构建入口；当前实际成功证据仍限定 LLVM22/MSVC142/19041，不据此宣称 v143 已验证。查询模式为 `ManualObservationControl --query-manual-controller PID`，返回被持有进程的创建 FILETIME；结束模式的 run/nonce、两进程身份、HWND 和 expiry 必须全部来自本轮原生 Ready 与 held 记录，由运行器绑定调用，不能复用示例或旧轮身份。

运行器必须先取得被持有 observer 的真实自然终态及 Job 清空证据，才调用独立 Control。自然非零终态仅可解除操作等待，原 observer 失败继续使整轮不合格；超时、强清、中断或身份不明不能发送正常结束信号。控制器直到 Host 正常退出前必须保持存活。原 150 秒活动/20 秒关闭预算不变，新的总控制期限只可缩短子阶段，不能重置或延长。

当前 gate 的 16 组、signal 的 13 组合同均已在 x86/x64 执行，页面 16 项合同通过；两个版本的 Host 使用相同源与 LLVM22/MSVC142/19041 编译，独立 x64 Control 编链和真实控制器身份查询通过。私有非输入桌面的单次正向窗口联调已实际验证 HWND/class、原 event/Control、Bridge 接纳、正常关闭后持久化及 checked-close；该 fixture 没有实际 observer，主动信号不代表观察结束，pointer/wheel 计数均0，也未实测提前点击。新两入口纯文件预检通过；实际人工配对仍单独验收，工具或功能证据不能认证物理输入、30 秒观察或帧率。

可选 `--engine-budget=0|1000|2000` 仅允许与手动模式组合，在加载 DLL、创建 Runtime 前设置并读回**本进程**环境；已有不同值拒绝，重复/非法值拒绝。原软件 harness 仍清除 GPU compositor/direct-present 与入口覆盖环境。`cold-profile.json` 仅保存任务三项控制的白名单读回：预算与两项 GPU 开关；任意未知字符串只标 `unrecognized`，不转存环境秘密。`requested_budget_us` 和 `observed_env` 不是 Engine effective getter，`engine_effective_observed` 始终 false。没有新增 SDK profile API 或默认策略变化。

ready/result 的 `compiled_sdk_version` 与 `compiled_sdk_package_version` 来自实际编译所用 imported `<soui/version.h>` 的 raw/major/minor/patch 宏。新 profile reader 要求两份原件都与请求的 SDK raw/numeric 身份相同；不能用旧 EXE 配新磁盘 header/lib 冒充候选。这是编译入消费者的版本旁证，仍需源码/头库/EXE/build evidence 绑定；不认证进程内实际 loaded kernel 或所有链接字节。

## scalar 原件与关闭资格

e2e 冷配置后、任何 Host/producer 注册前保存 `scalar-initial.json`（`soui.input-ui-scalar-boundary/v1`）。正常 Host/Runtime 关闭、UI detach、SDK/kernel stop 后保存 `scalar-sdk-final.ndjson`、有 kernel API 时的 `scalar-kernel-final.ndjson`，直接调用公共 `trace::exporter` 的原始 `soui.input-frame-trace/v1` 输出，没有详细消费者线程。`scalar-final-status.json`（`soui.input-ui-scalar-status/v1`）另保存初末身份、QPC 取数包围边界、完整 held snapshot 与资格原因。初末范围是整个应用生命周期，包含启动/关闭，**不是 30 秒输入窗口**。

快照完整保留 31 stage、16 counter、15×64 bucket 和所有 fault/registration 字段；uint64 使用十进制字符串。缺 kernel 为 null/API unavailable，不能填零。scalar 模式 attempted 与 recorded 不等是正常现象。资格还要求身份/模式一致、计数单调、最终 stopped/quiescent、没有 loss/saturation/owner/invalid/clock 故障、标准导出与 held snapshot 一致。导出期间源改变也拒绝资格。原共享 20 秒关闭剩余预算贯穿导出，不为每份文件重置；超时、已有文件或写盘失败保留失败证据并返回非零。

`io_complete` 只说明文件导出持久化，`all_requested_sources_usable`/host-result 的 `scalar_usable` 才表达两源 scalar 完整性，均不证明完整因果或物理输入延迟。状态文件写入本身也可能耗尽剩余预算，故 reader 必须同时检查 host-result、自然进程终态和返回码，不能单信某份状态文件。旧内核明确无 API 可保留正常生命周期退出，但 scalar_usable=false；要求两源的 profile 比较不能因此通过。所有文件 CREATE_NEW，不能覆盖原件。

无窗口合同目标 `SouiInputUiProfileContracts` 为 EXCLUDE_FROM_ALL，显式构建后以一个全新绝对证据目录作为唯一参数运行。它调用真实 recorder/exporter，核对原始导出、宽整数、manual cursor guard、故障源、缺源、倒退计数、关闭预算和输出碰撞，不加载 DLL 或创建 Host；通过也不证明输入/FPS 改善。具体独立源码/SDK/EXE 身份和实际执行结果必须另记录。

独立 CMake 的 `SOUI_INPUT_UI_SDK_VERSION` 接受仓库支持的完整发行身份：`X.Y.Z`、`X.Y.Z-preview.N`、`X.Y.Z-rc.N`，语法与 `tools/internal/sdk-version.cjs` 一致。空值每次从仓库根 `VERSION` 派生，不能把默认值永久缓存成旧版本。CMake 使用数值 `X.Y.Z` 做 `find_package(SouiSdk <numeric> EXACT)`，随后从实际 imported target 的 include 路径读取 `soui/version.h`，要求 `SOUI_SDK_VERSION_STRING` 与完整发行身份相同；`0.7.0-preview.1` 和 `0.7.0-preview.2` 即使数值相同也不能混用。

历史基线可显式传 `-DSOUI_INPUT_UI_SDK_VERSION=0.6.0`，同时用 `-DSouiSdk_DIR=<该版安装目录>/lib/cmake/SouiSdk` 锁定对应安装。已有源内非 imported `Soui::Sdk` target 时，以源码 `VERSION` 和源码头文件为身份；旧 benchmark 的版本 cache 不改变根工程已选定的源码 SDK，也不覆盖旧 cache。SDK 精确查找和头库配套要求没有放宽。配置会生成 `sdk-identity-<配置>.txt`，记录本轮 raw/numeric、实际头文件及链接库路径，构建证据应另保存其 SHA-256 和 EXE 身份。

纯 CMake 身份边界契约可从仓库根执行 `cmake -S benchmarks/input-ui-host/tests/sdk-version -B ../input-ui-version-contracts`，不编译或运行 Host。真实消费者仍需分别用对应安装 SDK 独立编译；版本解析测试不证明 GUI、输入或性能通过。

## 冻结请求与人工采样

操作步骤、请求字段、五类场景和未覆盖边界见 [MANUAL-SAMPLING.md](MANUAL-SAMPLING.md)。冻结操作只读输入并生成新 JSON，不启动窗口或注入输入。`run-input-ui.cjs` 接受 v2 冻结请求，启动前和结束后核对 EXE、SDK LIB、DLL、页面、配置、工具准备收据与测量源码；旧 v1 仅作模板输入，不再直接启动正式采样。

`burst-input.cjs` 的 `software_frame_lineage` 是独立的补充观察：在来源完整时，将唯一输入及正常 handler 返回关联到同代际的显式 processed coverage、frame/scene 和软件提交调用边界。它保留 stage8 原始空输入范围，不把关系改写为继承的输入 coverage。缺失、歧义、时序或 scene 冲突均拒绝该关系；异步 resources_ready 可选，未记录时保留 unknown。报告只保存前1024条诊断行，聚合计数/分布仍覆盖整个映射样本；关联工作有固定上限。

该字段不改变原 strict native stages、桌面输入分类、`fullCausalComplete` 或任何性能门槛。processed coverage 不能证明对应像素已变化，submit 调用返回也不证明 API 成功、可见反馈、GPU/DWM 完成或物理呈现。没有真实 microtask/visible terminal 证据时仍不得填补终态。新字段的合同可用 `node --test tests/benchmark/input-frame-trace/frame-lineage.test.cjs tests/benchmark/input-frame-trace/protocol.test.cjs` 验证；这些构造 trace 合同不代替真实输入测试。
