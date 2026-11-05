# 输入与帧性能后续记录（2026-11-05）

本文补充本轮 W02、输入来源定位、trace-scope helper 夹具结果，以及 Preview3 的 kernel/SDK 构建、隔离 prefix 安装和 SBOM 状态。原始采样、旧报告与旧汇总均保留；证据路径以验收包内 repair-r12/... 为准。Preview3 尚无完整运行包或最终体积验收，本轮不改写旧体积记录。

## 身份与范围

| 对象 | 本轮绑定身份 |
|---|---|
| W02 实测产品 | SDK 0.7.0-preview.2；SDK build ID c5da3a12-72b7-49e1-8195-2ceb1eda9985；build identity 91f9c251-v143；SDK source SHA-256 5bfcdf50c8e9a98e83c9d619afaf72712794c00608984d2b0e15c9df46c70c23；K1 产品状态未变 |
| W02 测量消费者 | 独立消费者源码快照 8e0221000f3af368d52decb9e751dd364edced5e，Host SHA-256 4d12d29f381afa00d78c4235190729ed3e50e27ab442f1385f6fe45ca10c1b6c。这是测量工具，不是 SDK 产品身份 |
| 输入来源诊断 | 既有 0.6.0 / K0 基线；诊断 Host 源码快照 6f01e8d6be620f12d1115994c866a9f9069045b6，Host SHA-256 7222d056c7424c423d3bb4d9258f1a9384e9ef1c51103d46694d5c948703dfe1；K0 node.dll SHA-256 d888637d5dd1476dde16b8c5bb28172d7b8ec3495b69c725fa3572013748139a |
| 当前 Preview3 构建候选 | SDK 0.7.0-preview.3，build ID b6684abd-4dc3-4fd2-916f-f4f73673090e，source SHA-256 554cf9b7bf8da27752f59e3a303311472d8de0bf99a3006f299bbc86da343ec1；kernel source identity dd53b24483eb824a2ab0854cfd308154f3190ed0f9720c2e9f81403ea967f11c |

W02 产品 SDK build receipt SHA-256 为 745aeb3a16458925c06124c620c93496c03f919b894a2b55fcf99f6457c71e9a，SDK library SHA-256 为 0caec28b8b39b0855e2a1ac322ec4ddb5e94bac217856cca1f8d68b4d28fa571；页面 bundle SHA-256 为 3c2ce56b92adb19f504b2bdb690beb0e606ed09fbee006385435cd8a79d45472。W02 使用 WebView2 后端、固定 156.0.4314.58 runtime、同一页面和 4 KiB Bridge echo 负载。输入诊断针对 K0 基线，用于定位旧的 source-loss，不代表已验证新的 K1 修复。

## SDK 0.7.0-preview.3：实际构建与导入

Preview3 kernel 的完整物理 source snapshot identity 为 dd53b24483eb824a2ab0854cfd308154f3190ed0f9720c2e9f81403ea967f11c（23,067 文件）；预期源码变化限于 gin/v8_platform.cc 与 V8BackgroundTraceScope.h。x64、x86 各重编译一个 translation unit 并与冻结的 K1 等价对象/库链接，ABI table probe 均通过。它不是 fresh whole-kernel build，也未达到 kernel manifest 所标的 v143 qualification。table 已加载，但 probe 未初始化 renderer、创建 view 或发送桌面输入。

Preview3 默认 kernel DLL 已导入 integration 的 x64/x86 prebuilt：x64 39,212,032 B（SHA-256 56d460c0b4136d85d3fcb8d4e4f00c19a04ff56532228031b55368742ef54308，相对旧 DLL +1,024 B），x86 28,324,352 B（SHA-256 bf6fd0bcb892f2997489a60d059671c9a618f4f99418d41f2c41f10ceae4dfb9，+512 B）。这是两份 kernel DLL 的字节差，不是完整 SDK 或交付包大小。导入只更新开发集成目录，没有变更用户安装。

SDK 0.7.0-preview.3 x64 Release 的官方 MSVC v143 build，build ID b6684abd-4dc3-4fd2-916f-f4f73673090e、source SHA-256 554cf9b7bf8da27752f59e3a303311472d8de0bf99a3006f299bbc86da343ec1，已自然退出 0/Job 0；同一 build receipt 经官方 verifier 后置核验通过。原 wrapper 因把 receipt 的 nativeBuild=passed 错读为 status 字段而失败，driver-error 原件保留，未重跑 build。之后该 x64 SDK 已安装到独立 CMake prefix；471 个文件通过 prefix 核对，总计 80,247,144 B。旧 Preview2 prefix 为 80,244,375 B，本次同口径差 +2,769 B。此数值只覆盖静态库、头文件和 CMake 配置，不含 kernel/runtime DLL、应用运行时或 ZIP；不能替代最终完整交付包体积验收。x86 SDK prefix/消费者和最终运行包仍待验。

Preview3 外部 SBOM 已生成；7 份源文档通过离线 schema validation、0 errors。随后 web SDK、React、Vue、native 四份 delivery SBOM 已 promote；promote 阶段只复核四份目标文件的 SHA，确认与此前通过 schema 检查的对应文件为同字节，没有再次运行 schema validator。更新后的仓库 validation 记录这四份文件、0 errors，并记录 136 个交付文件已重算散列。静态归档有 22 项被观察到，但未重新散列；transitive completeness 仍 incomplete，license review 未完成，runtime qualification 未推断，releaseReady=false。SBOM promote 不代表完整依赖闭合、运行验收或发布通过。

本轮没有 Preview3 前台输入、真实呈现帧率或性能采样。桌面输入在用户物理 Escape 后停止，未使用输入绕过；Preview2 的 W02 和 K0 的输入诊断仍严格绑定原身份，均不能转作 Preview3 结果。最终 Preview3 输入/帧率验收保持 pending。

完整打包状态也仍 pending：带 GUI 的包入口因缺少官方 GUI build record 在输出前拒绝；source-only 包在目录发布 rename 阶段连续 6 次遇到 EPERM，未产生新发布包且未重试。失败信息来自会话工具输出，另有外部摘要 sidecar；没有磁盘 raw 日志。隔离 CMake prefix 的 80,247,144 B 不是 ZIP/交付包大小。

## 新版消费者与补充合同

匹配 preview.3 安装头库的 x64 输入消费者已在 v143/Windows SDK 19041 下编译并自然退出 0/Job 0；profile/scalar 合同与 19 项 manual-report 合同通过，没有启动 Host、加载 DLL 或注入桌面输入。Web SDK 的 31 项合同通过、0 skip；Web SDK、React、Vue 三个 npm audit 均退出 0。独立生产源码、版本和来源审查为 0 阻断。

另一个 Performance Host 的实际 CMake configure 已返回 0，但该阶段拥有的 VCTIP 没有在原 30 秒收尾期限内退出，整个消费者尝试退出 1/Job 0，实际 build 尚未执行。该失败原件保留；不能因已有 cache 就宣称整组消费者通过。后续仅可对冻结 cache 执行尚未执行的编译与 42 项自测，不能重跑配置直到出现绿色结果。

## W02：PREPARE_OUTPUT 保持默认关闭

对照为每侧一轮 30 秒；只切换 SOUI_WEBVIEW2_PREPARE_OUTPUT=0/1。两侧 Host、SDK build receipt、页面、WebView2 runtime、负载和观测环境相同。OFF 的原始 run-summary 保留了旧 reader 对命名标量字段的读取失败；同一 raw 由版本化 reader 离线复核，侧车结果与原件分开保存。ON 正常完成。消费者 reader 的修正仅影响测量读取，不是产品改动。Host 和 sampler 两侧均自然退出，页面均完成保存和正常关窗。测量消费者另有 34 项 Node 合同通过（页面 33 项、关闭失败收尾 1 项，0 skip）；这是消费者合同，不是产品性能结果。新消费者 C++ Host 的 42 项自测尚未编译运行，旧 8e 二进制和本组采样身份保持不变。

- 4 KiB echo 成功数：OFF 38,638（1,287.93/s），ON 38,682（1,289.40/s），差 +44 次（+0.114%）。失败、超时、错配及待完成数均为 0。
- echo RTT 均值：OFF 0.9495 ms，ON 0.9969 ms；观测最大值两侧约 11.2 ms。
- 原生 WebView2 owner 时长直方图的 P95 桶上界由 0.2047 ms 到 0.1023 ms，P99 桶上界由 0.4095 ms 到 0.2047 ms。它们是对数桶上界，不是精确分位数。task queue 的 P95/P99 桶上界两侧相同（0.4095/0.8191 ms）。
- 完整且稳定进程集合的区间加权 CPU 为 OFF 2.6789%、ON 2.5528%。观测树 Private Bytes P50/P95 为 OFF 293,867,520/301,293,568 B，ON 302,997,504/308,649,984 B；Working Set P50/P95 为 OFF 497,270,784/505,454,592 B，ON 501,673,984/509,431,808 B。采样器未证明 WebView2 全生命周期进程均已观测，也未证明共享 browser 的完整归属，这些值只描述窗口内完整观测到的进程集合。
- input_queue 没有样本；本轮不是输入延迟或实际呈现帧率测试，也不能归因每次请求的 provider/COM 前缀耗时。

结论：这是一组有效的诊断性 A/B，不是统计性能通过。吞吐差异很小，现有数据不能建立可重复收益。继续保持 PREPARE_OUTPUT 默认关闭；不据此声称帧率或输入响应提升。

## 输入来源：原失败保留，关闭阶段已定位

R4 前台指针诊断以 125 Hz 发送 3,750 个输入，注入进程自然退出 0；Host 正常关闭 Runtime、Job 已闭合，但 kernel scalar 不可用，Host 退出 2，performanceGate=false。K0 的 unregistered_writes 在启动前、启动后、document ready、Host.destroy 前后均为 0；只有 Runtime shutdown 返回后的快照为 2。R4 原失败没有被重写或豁免。

R12 的 CDB 诊断中，3,750/3,750 个输入成功，Host 走完正常 Runtime 关闭，Job 自然闭合；CDB 在同一个 Windows thread-pool 线程观察到 K0 计数从 0 增至 1、再增至 2。调试器退出码 2、scalar 不可用，因此这不是性能数据。根因回执把两次写入归到 K0 的 V8 background WorkItemCallback 路径，回调内调用 v8::Task::Run；具体子 V8 task 类型及哪项内部 trace 操作触发写入仍未知。回执中的一个运行时 VA 已由独立 correction 文件更正；本文不依赖数值地址作结论。

## 修复与夹具验证边界

ADR-0162 对应的 gin::WorkItemCallback 内部 registration 和 V8BackgroundTraceScope helper 源码已实现。独立审查发现早期合同夹具的 10 秒等待失败路径可能在回调仍运行时释放栈状态，这是测试代码问题，不是生产 helper/gin 的发现。测试路径已修正：保留 10 秒等待，等待失败时以非零 24 终止夹具进程；不延长期限，也不把超时当成功。审查确认该失败路径修正在源码中成立。

当前源码指纹的 x64/r3 与 x86/r4 fixture 均已构建、自然退出并闭合 owned Job；两架构均为 build 0、缺 scope 负例 23、scope 正例 0，且没有 watchdog fallback 或强制终止 Job。测试源码 pin 为 d4a09f95f33714730807a77b73a3a9f9c0413916d7eca1d5ca06caf71636422d，产品 gin/helper pins 未变。早期 x64/r2、x86/r3 原件继续保留，但不代替修正后 fixture 的身份。fixture 只验证局部 scope helper 正反契约，不证明实际 V8 生产内核完整执行路径。

修复只在已定位的 gin::WorkItemCallback 内做 registration，不自动登记所有 post/record 热路径，也不放宽 source-loss 断言。该改动已经进入 Preview3 的 x64/x86 kernel DLL，并随 x64 SDK Release build 通过编译和链接。但没有新 kernel 的 renderer/runtime/真实输入复验；既有 K0 计数器 failure 仍是原样保留的旧证据，不能据新 DLL 的构建结果称 source-loss 已解决。帧率、输入性能门槛和发布状态仍未通过。

## 证据索引

以下散列用于核对本轮原始依据。

### Preview3 构建、导入与 SBOM

| 证据（验收包相对路径） | SHA-256 | 要点 |
|---|---|---|
| repair-r11/integration/third_party/manifests/kernel-build-070-preview3.json | 50381f92fe6ea6fe7246b4658f0ccbb1283c3cf9720351699b6e96328cfdd7c2 | Preview3 双架构 default kernel 导入清单 |
| repair-r11/integration/third_party/manifests/kernel-v8-background-build-070-preview3.json | 3a66f48b3a240c87168dce47a7dfdb0fca658c0ca5c324c0485a9fc79eb9d90e | source snapshot、one-TU build/link、ABI 和非资格边界 |
| repair-r12/v8-final-source-snapshot-r1/source-input-snapshot.json | 502c14408e2293e11a743ed25648e3bf94d43c9fd18f5ea82e74d3865af83903 | 23,067 文件的物理 source snapshot |
| repair-r12/v8-kernel-fix-r2/x64/result.json | 802e8df51faf001173fba79cbbbf204eee8370a6d8b6aa91e8df7c0ad1853ee4 | x64 一 TU 编译/链接；freshWholeKernel=false |
| repair-r12/v8-kernel-fix-r2/x86/result.json | 367ef09b5007e3ec15de4bea1fa4ea1f1fb92dce396ec921bb55d5a2a0aa805e | x86 一 TU 编译/链接；freshWholeKernel=false |
| repair-r12/v8-kernel-abi-x64-r2/summary.json | de369cf81a2508914db82f23b8949b359d5255792701c6278f8d2d295d212a6d | x64 function-table ABI probe；未初始化 renderer/view |
| repair-r12/v8-kernel-abi-x86-r2/summary.json | a462ac322ee096bb34a4f3b67b31016434a0628c1d814a7707fd9a20042b2d3d | x86 function-table ABI probe；未初始化 renderer/view |
| repair-r12/v8-kernel-default-import-r1/result.json | 9a2242d15537058ef56d5bec1ade741d2da6300ded08984db54683ca58544060 | Preview3 DLL 已导入 integration 默认目录；未改用户安装 |
| repair-r11/integration/third_party/patches/miniblink/0034-v8-background-trace-producer.patch | 6fd48adba151df110d7b538ce081414f91e3610cea2a241835376c608b71b60b | 已识别 producer 的 source delta |
| repair-r11/integration/third_party/manifests/dependencies.json | 2b78348ba6fcefca53be7c6d36e4399e439e5d6f7bac7dcae004f5e496b4b23c | Preview3 dependencies manifest；releaseReady=false |
| repair-r12/preview3-sdk-x64-r1/posthoc-build-verification-r1.json | 399fff8e20aa0ebb58478f58625c7897f9e97765776d5930984aa0d7118e54ab | 同一实际 SDK build 的官方 verifier 后置核验 |
| repair-r12/preview3-sdk-x64-r1/build.result.json | 3b43a6311c9861118d46a729f979411a04d19995399cc4d09a2676131c994967 | SDK x64 Release 实际自然退出 0/Job 0 |
| repair-r12/preview3-sdk-x64-r1/driver-error.json | fe2dbd24866ccb77ad03d2e45a0abcf45ababbab66ce3591c3134c19fab82cc3 | wrapper 原字段误判失败原件，保留 |
| repair-r11/integration/out/sdk-x64/build-Release.json | 1b7809984ee35fcdbd12b9d08e05f8cd77c404636e6cbc7eefaf8b41879b5424 | SDK 官方构建收据；后续已安装到独立 x64 prefix |
| repair-r12/preview3-installed-x64-r1/summary.json | 0cc02f4862574fb6a0a7700edfd5c70669edbaa80946c6df941408de67122319 | 独立 prefix 安装摘要；471 files，80,247,144 B；未做 runtime/performance qualification |
| repair-r12/preview3-installed-x64-r1/install.result.json | 628613653dc43c10b65e626fa1d725af75ad690998dd3eed722754f116acffec | 安装自然退出 0、Owned closure 确认 |
| repair-r12/preview3-cmake-prefix-size-r1.json | 7e0a4a5a60786144322fde5d82269855312da3c5aebf9cec1807fc3f75f1cade | 两侧各 471 files 的完整 prefix inventory/hash；差 +2,769 B，不含 runtime/ZIP |
| repair-r12/preview3-delivery-limitations-r1.json | a791a2e484acb854b1438b28af5033453bf28567b8e775a341d5845107e4c559 | 带 GUI 包缺官方记录被拒；source-only rename EPERM 六次；Escape 后输入/帧率 pending |
| repair-r12/preview3-sbom-r1/artifacts-r1/Validation.json | 02def75bc9cec52ccf88449343241e47d7fff86051533ec24c73a3951b46d09b | 原始外部生成验证：7 文档 schema-valid、0 errors；不是最终 promote 状态 |
| repair-r12/preview3-sbom-r1/artifacts-r1/Assembly.json | 6ba9b0057176f52c3d2d06b119de111ea9be9c05b31043aa47398f892654e1b6 | 外部 Assembly 生成记录；promote 状态以新回执为准 |
| repair-r12/preview3-sbom-r1/artifacts-r1/bindings.used.json | dc5ebef37f31767ab7e7574078aeb681d0a7f6139e9dd2dfb27ba4db4464a254 | SBOM source、build/import receipt bindings |
| repair-r12/preview3-sbom-r1/PREPARATION.json | add6c73c3d50f7ef0ce15556bdde6a2728ed32dc84907dd77311cbbb9a11199f | external SBOM preparation and generation binding |
| repair-r12/preview3-installed-x64-r1/sbom-promotion.json | 264c5b990a7acca8d325db3f73d9b95981f7b5ff4fde7495c7b1e37234a91368 | 四份 delivery SBOM 的 promote 与 validation 绑定 |
| repair-r11/integration/verification/sbom-native.validation.json | dfa5a2b86b8f0ebed283e4192982f1b5885c815f60639a48a159dbd50e1aac1b | 当前四份 delivery SBOM 的 SHA 绑定记录；promote 未重新运行 schema validator，transitive/license 限制仍开 |
| repair-r12/progress.json | bb0f9b8cee9d5018d06b7883ee1325625f5dee6419e6f7a1e3bbb539e2acdc58 | 当前安装、SBOM、失败打包与输入待验状态快照 |

### 输入和 W02 原始证据

R4 六份边界快照直接显示计数变化：

| 证据（验收包相对路径） | SHA-256 | 要点 |
|---|---|---|
| repair-r12/pointer-lifecycle-baseline-r4/summary.json | 2032296fac31e3d65f1dc40cf45a39b1ac4f23933fb860a7bcd0f7f64150ecb8 | R4 失败与自然关闭状态 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-before-start.json | 8e6b077855d3a4b69394912a3305f02ae4df4275bbba898fb908259306cd2e25 | before_start：0 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-after-start.json | 77b4f1016e6590c60663a640c9c7a13fbc2d8fcef16c50074f135bd72d1f098b | after_start：0 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-document-ready.json | 9e8037ee5abc2c500f061001366c2ccc83858aea11ee14c4e8e599d54869e740 | document_ready：0 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-before-destroy.json | 9e8529a2f060a7e90acc58f7ef5ed392fe78a025392a07c39357ecbf318b145b | before_destroy：0 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-after-destroy.json | 9897192fecadb8fc93d9162ced6438aa280491516d839c4921ba20d55a1f83a5 | after_destroy：0 |
| repair-r12/pointer-lifecycle-baseline-r4/host-output/scalar-diagnostic-after-runtime-shutdown.json | d80d8cc97b5d5faf9586918e94cd7d61d60a4bead0929dc4dd355172e3de3a3d | after_runtime_shutdown：2 |
| repair-r12/k0-counter-watchpoint-static-proof.json | 5ea27ffc1f62b7c6c8f901d4ce4cf8288f5ccbea2e945d43e96ef9fe4b16c009 | K0 计数器静态定位；非运行时结论 |
| repair-r12/pointer-cdb-baseline-r12/summary.json | 67a3d94052bf448ba58665a8585c01e1f1d0423f581278091c4142849982764c | CDB 诊断状态、2 次 watchpoint |
| repair-r12/pointer-cdb-baseline-r12/debugger.log | d1156704451a0558d81c2fcdde3b0ce36b4846b7b87d05e512e8df7fc7a34949 | 实际两次 watchpoint 与栈 |
| repair-r12/k0-v8-background-producer-root-cause-r1.json | 40dd26fb955d6fab5c9b2393aec79ea374e289a2486352b61f8b08eba6094891 | WorkItemCallback 路径归因；数值 VA 见独立更正 |
| repair-r12/k0-v8-background-producer-root-cause-r1-correction.json | cf237b02023304ccd6d0130ff3183fc4d73c9ec748b0c6e5efac70a04a58ecf1 | 根因回执运行时 VA 单字段勘误 |
| repair-r12/v8-background-scope-independent-review-r1.json | 27aa9bdc6804a19e486a5f0222263541822979534fd573a5c872f7264f551f20 | 原 fixture 超时失败路径 UAF 审查 |
| repair-r12/v8-background-scope-independent-review-r2-addendum.json | 024c6f1c8c3769228f87f39b8546994f4b1639f61c046ea7f74206700f312590 | 仅测试失败路径修复；生产重链仍待执行 |
| repair-r12/trace-scope-fixture-x64-r3/build.result.json | c7f8994efa4e87993091d50c7a1c57a29de7fd7ff3acaad02ec386563ad572fa | 修正后 x64 fixture 构建自然退出 0 |
| repair-r12/trace-scope-fixture-x64-r3/negative.result.json | cc9691c4eb9cbe533c1510ebfd113c1bfa99f0addfe0b5c63a5668b51d5087cd | 修正后 x64 缺 scope 负例自然退出 23 |
| repair-r12/trace-scope-fixture-x64-r3/positive.result.json | 5ca4486809288401a9eed35ab87043a045867f7f93942579d9bde484b06fee37 | 修正后 x64 scope 正例自然退出 0 |
| repair-r12/trace-scope-fixture-x86-r4/inputs.json | 77fc2a204e9cae1f732ff04033c84b3bace3e212f9a6053666c5540470ba1dc5 | 当前 x86 fixture 输入与源码 pins |
| repair-r12/trace-scope-fixture-x86-r4/build.result.json | d25e83484d8a9996b86f9c27b92ade709ede68195d292ded51199edd7de20557 | 修正后 x86 fixture 构建自然退出 0 |
| repair-r12/trace-scope-fixture-x86-r4/negative.result.json | f073e8a12469d83fbbd002a1648970951b9d008b1413b7533b812f5b78e68c4f | 修正后 x86 缺 scope 负例自然退出 23 |
| repair-r12/trace-scope-fixture-x86-r4/positive.result.json | 69654091d325b053ae557bf3b017955d66df5b04e4c258a9e3d7cedc5c8e415e | 修正后 x86 scope 正例自然退出 0 |
| repair-r12/v8-scope-fixture-summary-r1.json | 443209661690046a44b11080aaa4b2afe43aabcb6ce44e5956404974d666ff0e | 当前源码双架构 fixture 汇总与自然 Job 闭合证明 |
| repair-r12/trace-scope-fixture-x64-r2/build.result.json | b347c0a997e7124e697fdc3d1e7a4126ddf360d197238c4b377171abfce7b404 | 旧 x64 fixture 原件；修正 timeout harness 前 |
| repair-r12/trace-scope-fixture-x64-r2/negative.result.json | e2a4a492fa8c71062bce684cc0f99144aec384a534c0ebed084149439b315a02 | 旧 x64 缺 scope 负例，保留原件 |
| repair-r12/trace-scope-fixture-x64-r2/positive.result.json | 2ef8f1b7f5dcbd9cc4200be84fda331c3d7beeed8a5c8aff2de3872e4bf7b396 | 旧 x64 正例，保留原件 |
| repair-r12/trace-scope-fixture-x86-r3/build.result.json | 655ac82710bf61d57cb517066d12f7bcdff949d64995ce4fa4bc72bd53f317c7 | 旧 x86 fixture 原件；修正 timeout harness 前 |
| repair-r12/trace-scope-fixture-x86-r3/negative.result.json | e03965e9f9961fe8c2d7770b186f7ce8122456903c38145927e8786e22e176f6 | 旧 x86 缺 scope 负例，保留原件 |
| repair-r12/trace-scope-fixture-x86-r3/positive.result.json | edb1415b6c7d04fb8c0e77808eb4ee71d579543e450d0663d5bc9409d4f4fe0a | 旧 x86 正例，保留原件 |
| repair-r12/w02-reader-r1/pair-comparison-r2.json | d440284ae790a682de61fcbee627ebe793d46559028e80f8f91fc0a905d87483 | W02 配对指标、限制和资格 |
| repair-r12/w02-reader-r1/off-postflight-sidecar.json | 900483be73dc784a794d807eb27b94e1a2bd9d1e99a5598b16f4af791b28258f | OFF 原始记录的独立 reader 复核 |
| repair-r12/w02-auto-results-r2/off/run-summary.json | 61b3ff98f79bae331d61a561110d01e45bff3e746afe34872d84743fe1bbc9e3 | OFF 原始汇总，保留旧 reader 失败 |
| repair-r12/w02-auto-results-r2/on/run-summary.json | e5439453a24c241018b4b4068459504ff95c8cd28a9a6e9e01d33e6ee6732fbc | ON 保存与正常关闭 |
| repair-r12/w02-auto-results-r2/off/host-output/report-3.json | c27e85bb59b90b3542638d23b3129f17642fcb08a97bcc641dacf96ea414c7b5 | OFF 页面报告 |
| repair-r12/w02-auto-results-r2/on/host-output/report-3.json | 831c28abafb6e09887136112358521634aa98bd0723716e6f6c4b1ca8e7251f4 | ON 页面报告 |
| repair-r12/progress.json | bdb700890891b80611cc0f88da65b1fb49a959593edcadd83c66dc50b64bee39 | 更新后的 Preview3 构建、SBOM 与旧输入状态快照 |

## 后续必须补齐

| 项目 | 当前状态 | 新结果归档后填写 |
|---|---|---|
| 根因回执中具体 VA 的勘误/复核 | 已有独立 correction 文件 | repair-r12/k0-v8-background-producer-root-cause-r1-correction.json 及 SHA-256 |
| 修复源码快照 / commit 与变更清单 | helper 与 gin 源码已实现；fixture failure-path 修正独立 | 待冻结 |
| 当前源码指纹的双架构 fixture 结果 | 修正后 x64/r3 与 x86/r4 均完成 | 已由证据索引绑定 |
| Preview3 kernel 与 x64 SDK 身份 | 双架构 patch DLL 已导入；x64 SDK build 已验证并安装到 471-file 独立 prefix | x86 SDK prefix/消费者与最终 runtime 身份待补 |
| 在 Preview3 kernel 上复验 R4/R12 边界：Runtime shutdown 后计数仍为 0，且原始 source-loss 断言保持严格 | 尚未执行 | 新 DLL 的运行时 source-loss 复验待补 |
| Preview3 前台真实输入、帧率、完整 trace 资格与适用性能门槛 | 尚未执行；用户物理 Escape 后停止窗口输入 | 保持 pending，不使用旧 Preview2/K0 结果代替 |
| 完整 SDK/运行时/ZIP 体积与交付 | CMake x64 prefix 80,247,144 B；带 GUI 打包被拒绝，source-only 发布 rename 失败 | 最终完整包及体积验收待补；prefix 字节数不等于交付包大小 |

Preview3 已有双架构 patch kernel、x64 官方 SDK build、x64 独立 CMake prefix 安装和四份已 promote 的 SBOM；runtime、消费者矩阵、真实输入/帧率及完整包仍待验。不能把 K0 诊断或 c5/K1 的 W02 结果转移为 Preview3 性能结论；也不能把 kernel DLL 增量或 CMake prefix 字节数当作最终 SDK/交付包尺寸。旧体积记录保持原样，最终打包与 size verification 尚未完成。
