# 输入到可见内容：关联及同产品 burst 协议 v1

本协议源码未编译、未执行。它与现有 SOUI/Electron、serial-ACK、96DPI 桌面 v1 reader 分开；原有文件及门槛不变。W01 recorder 的阶段、终态、整数布局见 ADR-0125 和 SDK/vendor 镜像头。

## Recorder 与导出

启动方先产生非零 128-bit run_id；同一进程的 SDK 与内核分别以 `current_identity(high, low)` 取得真实 PID、创建 FILETIME、QPC frequency，在任何线程注册前 `configure`。每个实际 owner 冷启动显式注册，退出显式 detach。一个进程仅一轮 session，重跑使用新进程。每模块最多 32 个生命周期注册流，详细模式每流 1024×136 字节；scalar 不分配事件数组。注册用尽、未注册、错误 owner、非法数据、时钟、溢出和 causal_lost 均拒绝完整链路资格。

exporter 由非 engine consumer 有界 drain，默认每次 256 项、最大 1024；每个 exporter 最多 100000 事件（POD NDJSON 小于 64MiB）。到上限仍有排队项时标 limit_reached/causal_lost，拒绝完整 end；不能继续无限落盘。不要为落盘向 engine FIFO 投任务。先 begin，可边运行边 drain；停止后须等实际 producer handle 释放，再 drain 到空，finish 才写 complete end。SDK/vendor 可分别导出，通过精确 run/process/QPC 身份连接。所有 uint64 字段都是规范十进制字符串。没有采样模式；若将来增加，必须另行版本化完整性声明。

`current_context`/`context_scope` 保留已建立的上下文，不创造可见性。新 task 用 `next_task_id`；SDK 使用低 63 位、vendor 使用高位，parent task 放 task 事件 `related_id`。最多 8 次 CAS，失败记录 causal_lost 并返回 0，产品工作继续。注册和 context/range 上限不允许丢产品输入。直方图使用整数 QPC tick 的 log2 桶：桶 0 包含 0/1，桶 n 包含 [2^n,2^(n+1)-1]。

冷启动诊断 storage 分配失败时 available=false，各操作安全拒绝，不能使应用或 DLL 启动失败。默认 scalar 仍具有实际 PID/FILETIME/QPC frequency，零 run_id 不允许正式导出。duration_metric 0–8 保留原语义，9–14 分别为 engine_native、engine_provider、engine_input、engine_fifo、webview2_owner、serialization，直方图共 15×64。

v1 ASCII NDJSON 的保守字节上界：每 point 13 个 uint64 均最多 20 位，固定键与有限 uint32 字段合计少于 640 字节；header 少于 512 字节；31 stage、16 counter、15×64 duration 与其他字段的 end 少于 32768 字节。因此完整 exporter 上界为 `100000×640+512+32768=64033280` 字节，小于 64MiB。exporter 冷启动固定经典 locale/十进制并清 width，consumer 从 begin 到 finish 独占 ostream；自定义 WKE/落盘 consumer 同样必须同时限制总项数与总字节。

stage29 `interaction_diagnostic` 和 stage30 `gpu_present_diagnostic` 仅替换实验热路径文件日志。它们由全新 event 构造，`why/outcome/flags` 与全部 view/generation/input/task/frame/action/related 关系字段必须为零，只有 `value` 使用镜像 header 的固定 `interaction_diagnostic_code`（1..20）或 `gpu_present_diagnostic_code`（1..4）。thread/producer/QPC 仍由 recorder 正常校验。这些数值只说明诊断分支经过，不证明 admission、资源资格、frame submit、coverage 或 visible，也不能输入到 native causal-context API。旧31维以外 stage 一律拒绝；两端 header/库与 reader 必须按同一冻结提交配套。

事件 offset28 的 producer_slot 由 recorder 发布时写为 1–32；调用方和 context setter 必须提供 0。它区分生命周期注册流，允许 OS thread_id 复用。reader 只要求同一 slot 的 owner/QPC 一致，允许旧/新 slot 在 drain 时交错，不按全局或 OS thread wire 顺序强行排序。同 tick 跨 producer 顺序明确未知；tick 量化与工具误差另报，不捏造严格先后。

静态、未执行的 reader 入口：

```powershell
node benchmarks/protocol/input-frame-trace.cjs <trace-sdk.ndjson> <trace-vendor.ndjson>
```

有丢记录时此 CLI 返回 2；数据格式/身份错误返回 1。`complete:true` 仅证明 producer 已释放且缓冲排空，不代表每个输入都建立全部阶段。零 ID、未知 frame、缺少 terminal 和 coverage 必须保留为缺口，不能按时间接近补关系。processed-context coverage 不能直接当可见内容证明。

## 连续输入源码与冻结页面

`BurstInput` 复用 OwnedProcessRunner 的监督约束；新增 `burst-input.cpp`、独立 `burst-desktop-observer.*`，不改旧驱动/reader。目标需同一非零交互 session、一个前台 1100×720 窗口、96DPI、DPR1、单个非旋转输出。`burst-page.html?run=<32位小写hex>&kind=pointer_move`（或 wheel）是受控页面。marker 与反馈背景同一次 canvas 更新，collector 同时读取真实背景像素并核对序列，不能单独更新 marker。

```powershell
<BurstInput.exe> <目标PID> <持有句柄的创建FILETIME> <run_id> <125或500或1000> <数量> <新的绝对burst-input.ndjson> <新的绝对burst-desktop.ndjson> <pointer_move或wheel>
```

每轮 1–100000 条，最多 120 秒。指针轨迹使用 1000×100 不重复位置，起点 (100,100)，DOM 直接解码坐标为序列。wheel 在基线前定位一次，每条固定 -120 单位、`sent:1`，DOM 用实际 wheelDelta 累积单位；原生聚合仍保持数量守恒，不把 DOM handler 次数冒充注入序列。SendInput 部分失败、无目标/会话、并行 injector、计时失败、失焦、窗口/输出变化和 final ACK 失败均以非零退出。输入记录预分配，注入期间不格式化或落盘；一个 waitable timer 逐 deadline 等待，不逐事件等待 ACK，不忙等，不提高实时优先级，不改变全局 timer 精度。详细 trace 自己仍须同步 drain，否则会因 1024 上限拒绝完整性。

诊断 tag 为 `0x53400000 | sequence`，低 20 位不回绕；x64 上层 32 位必须为零。host_receive 的 related_id 只记录有效 tag；actual input sequence 保留 router 身份。tag 没有授权意义，reader 还要求精确 native held target/injector、run、QPC、注入范围和唯一映射。tag 缺失或 OS 合并不会被改写成已接收输入；单列 unobserved 和未终结序列。

新桌面协议允许 ACK 跨越和有界 accumulated images，保留遗漏数量。仅对实际观察到、同时匹配内容像素的 ACK 计算 captured-image 样本；跨越的输入不自动获得“已显示”终态。最老未显示输入年龄不会因较新 ACK 消失。遗漏图像不能证明真实首次显示；物理发光未测。`calibration.json` 须绑定本轮 run/QPC，并记录 `schema:soui.burst-calibration/v1`、`observation_error_ms`、`device_pixel_ratio:1`。误差值由专人实测冻结；reader 不认证自报校准。

NEW burst marker 将20-bit delivered_units 放入 byte14/15 与 flags高4位，CRC同时覆盖这些位；旧 marker/reader 完全不改。指针计数只在严格递增、合法唯一坐标时加一，重复/倒序使 marker 失效；滚轮计数是实际累计120单位。只有冻结受控页面、单 injector、内容像素、完整图像历史，以及 delivered_units==ACK 的共同证据，才能把未单独呈现的范围记为 visual-coalesced。缺任一证据仍为 unknown，不能仅凭 ACK 跳号分类。终态各类与 unknown 总数必须等于注入数；coalesced 不是新 T7 样本。

## 同产品两个提交的证据目录

每轮保存 `input-ui-run.json`（`soui.same-product-input-run/v1`）、`payload-manifest.json`、`environment.json`、`configuration.json`、`workload.json`、`calibration.json`、两份 burst 原件、`trace-sdk.ndjson`/`trace-vendor.ndjson`（现代后端单列）、原样冻结的 `page-bundle/burst-page.html` 与 `.js`，及原生 host/input OwnedProcessRunner 的 report/context/stdout/stderr。`owned.verifyOwnedProcess` 重新核对自然退出、Job、PID/FILETIME、原件散列、真实 executable/runner 散列与 launch context；不能用 STOP 转自然完成。

run 字段包含 product_id、source_commit、run_id、capture_root、renderer（miniblink-software/webview2）、runtime_version、trace_mode、host_exe_sha256、tools.input_sha256/owned_runner_sha256、host_arguments、processes、owned_evidence、traces、evidence。evidence 中每项为 `{path,bytes,sha256}`，对应固定文件，包含真实 host-ready/result 身份和正常关闭/QPC起止。payload manifest 为 `soui.input-ui-payload/v1`，列 source_commit 和 files（role/path/bytes/sha256，至多 256），host role 必须与原生启动散列相同。它与现有来源/运行载荷清单配套，不能证明已加载 DLL 的内存身份。

workload 为 `soui.burst-workload/v1`，冻结 kind/hz/requested/content_width/content_height/page_html_sha256/page_script_sha256。environment 为 `soui.input-ui-environment/v1`，记录隔离物理目标、锁屏/RDP/拓扑变化和 invalid_reasons；这些记录须由验证人员核查，reader 只检查一致性。configuration/workload/environment 在一对中必须字节相同；源码 commit 和 run_id 必须不同。同产品、renderer、Runtime 和工具保持一致。

```powershell
node benchmarks/protocol/same-product-input-pair.cjs <baseline结果目录> <candidate结果目录>
```

以上命令仅做证据一致性检查，不放宽已有源码、协议或来源门槛。至少五个有效配对和每候选至少 10000 条具有可判定终态的输入由专人继续执行；P99 样本数只用真实可见样本，不用总注入数代替。详细 trace 的开关预检、真实输入/IME、资源、后台进展和物理显示仍为独立验收，所有工具始终不授予 performanceGate/releaseReady。

回归源码入口（未执行）：`tests/native/input_frame_trace/recorder_tests.cpp`、`node --test tests/benchmark/input-frame-trace/*.test.cjs`。原生 preflight 用例仅在显式提供 `BENCHMARK_BURST_INPUT_EXE` 和 `BENCHMARK_OWNED_RUNNER_EXE` 时执行；它用持有的 Job 检查无目标/错误创建身份、非零退出及无注入/输出，保留失败原件。真实 session 丢失、部分 SendInput、关闭/焦点/显示拓扑和内容像素路径由专人执行，不以合成协议用例代替。

## 一轮与系列的执行交接

`run-input-ui.cjs` 是新的单轮 Owned orchestration，直接启动实际公共 SDK Host 与 BurstInput，等待并核对 native ready/host-ready，两个 Job 都自然完成后生成 run manifest，保留所有原件。失败只停止持有的自有 Job；不自动重试、不猜测环境根因。请求 schema 为 `soui.input-ui-run-request/v1`，字段是 product_id/source_commit/measurement_source_commit/runtime_version/trace_mode/kind/hz/count，及绝对路径 host_executable/input_executable/owned_runner/miniblink_binary/sdk_library/page_root/frozen_environment/frozen_configuration/calibration_template。measurement_source_commit 固定同一量测源码，core source_commit 分别为基线/候选。校准模板 schema 为 `soui.burst-calibration-template/v1`，含 qpc_frequency/observation_error_ms/device_pixel_ratio；本工具不替专人制造校准证据。

```powershell
node benchmarks/protocol/run-input-ui.cjs <冻结请求JSON绝对路径> <新的结果目录绝对路径>
node benchmarks/protocol/same-product-input-series.cjs <系列结果根目录>
```

正式对照用两端相同 e2e 模式；旧基线无内部 API 时明确 stages unavailable。候选 full 模式单列诊断，不能拿非对称详细 trace 当正式收益。T1→T6/T7 有实际证据即可单独统计；缺少微任务关联继续 unknown，不妨碍已证明端点样本，但 complete-all-stage-chain 维持 false。资源 CPU/内存/后台进展、trace开关扰动仍须用既有 ProcessTreeSampler/相关标量能力按相同 held subject 一次配套采集；缺失不视为零或通过。

系列 manifest 为 `soui.same-product-input-series/v1`，scenario 与 attempts（最多64）记录每个ordinal/status/path。valid 项另含pair/role，第一对 baseline→candidate，第二对反向，如此交替；实际 host QPC 生命周期必须不重叠，commit/tool/config/workload/measurement source 冻结。至少5有效对和候选累计10000已分类输入只授予 repetitionEvidenceAccepted，不授予性能通过。每轮分别报告事件级 P99、实际stage/T7样本、最大值和条件 DKW rank-band；burst样本相关性未证实IID，95%仅为条件名义值，不能当已验证置信区间，更不对五个均值算P99。

`nextAction` 是启动前必须调用的有界 orchestration contract：同场景连续两次相同 environmental invalidation 后 stop；一次 product_failure 即停该候选。失败目录保留 attempt-invalid.json（schema `soui.input-ui-invalid-attempt/v1`、scenario/status/reason/retained_raw、1–16个真实 raw_evidence 描述符）；原因由专人核查。checker拒绝在停止点后继续同场景，CLI返回2表示统计门槛不足或已停场景。旧61s/123s serial协议完全保留；新burst可用短轮次避开详细trace总量上限，不能互相冒充。
