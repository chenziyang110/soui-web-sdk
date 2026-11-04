# SDK 输入与帧率优化：2026-11-05 验收报告

结论：**实现及定向正确性验证完成；性能改善未获验收通过。** WebView2 的桌面内容变化对照未显示超过预设噪声的变化；MiniBlink 的基线复测漂移，不能判定收益。输入基线出现两次未注册诊断写入，本轮输入 A/B 不合格。`performanceVerified=false`、`releaseReady=false`，PR 保持 draft。

本报告补充既有记录，不覆盖历史失败、收据或主线验收状态。没有启动 Electron，没有改变 Scratch、画质、角色数量或 SDK 默认策略。

## 源码、工具链和载荷

- 方案基线：`a4fc394427c99a3331abfb2e3e3307ae27503dd0`，SDK 0.6.0。
- 实际候选 SDK：0.7.0-preview.2；数值 package version 0.7.0。本地源快照 `91f9c251c23ca8167c6824e646eb80d3d13c6dfd`，源码集合 SHA-256 `5bfcdf50c8e9a98e83c9d619afaf72712794c00608984d2b0e15c9df46c70c23`，6,092 个构建输入逐项绑定。该本地快照包含原工作区资产，不作为待提交的完整树覆盖远端。
- 最终 x64 SDK-only / WebView2 构建：官方 `Build.ps1`，MSVC v143 14.44.35207 / compiler 19.44.35226、Windows SDK 10.0.19041.0、Release；Build ID `c5da3a12-72b7-49e1-8195-2ceb1eda9985`，通过并自然退出。构建收据 SHA-256 `745aeb3a16458925c06124c620c93496c03f919b894a2b55fcf99f6457c71e9a`。
- 基线同工具链 SDK Build ID `e3ecec57-2ec7-48ba-817e-5a749b8e2e17`。两边各安装 471 个文件；消费者分别使用匹配的公共头、库和默认内核，不用标签替代载荷身份。
- 最终普通性能消费者候选 SHA-256 `23993677…`，基线 `539976f5…`；输入消费者候选 `264bda87…`，基线 `7eb7056a…`。完整值及安装身份见机器可读证据索引。共同配置为 feedback OFF、engine budget 0、请求帧率 60。
- 远端集成基点 `e561ca0b8bc9284e7f2f4e03c7d32b31776964a2` 已包含后续 WebView2 诊断关闭守卫、MSVC 早期占用预检及 LLVM 消费者工具链修复。本轮在独立工作树保留这些改动、主线托盘/关闭/文档与既有验收文件，仅合入本任务增量。

最终整合树与实际测试树的 `sdk/`、`cmake/`、`third_party/include/`、`third_party/manifests/` 共 1,063 个已跟踪文件逐项核对无缺失/差异（文本仅归一 CRLF/LF，二进制比较原字节）。这证明所列产品源码投影一致，不是对含最新报告/读者修改的整仓库重新执行构建；原始构建和运行收据仍绑定原身份。

本地测量源码修复单列为 `eb31bec2dcd6dafd17ccf2a39748ed8ce12c52f0`，只改变 JS 读者和测试，不改 SDK、DLL、消费者 EXE 或原始记录。完整因果诊断的旧失败原件保留，修正读者的回溯分析写入独立文件。

## 帧观测方法与结果

同设备、同窗口尺寸、96 DPI；Windows/QDC 报告单活动 HDMI 路径及 1920×1080 / 60 Hz 配置，WMI 的活动 EDID 与目标 ID 对应。这只是系统报告配置，工具的 `physicalTargetConfirmed=false`，不证明物理目标或实际扫描刷新率。共同固定 Canvas 240 角色、逻辑 480×360、seed 1374772973、60 次/秒模拟目标；每轮独立观察 30 秒，先冻结 A1 噪声，再读取候选。编译、压缩与采样串行。

显式 `gdi-bitblt` 观察器请求 4ms 轮询，实际 BitBlt 约受 16ms 捕获节奏限制。以下为**所采样的桌面内容变化率下界**及观测变化间隔，不是精确唯一帧率、物理扫描输出或 photon 延迟。rAF 仅用于页面身份和准备，不充当桌面帧证据。不同后端使用各自固定 ROI；WebView2 内区 479×359 根据同一实际 DOMRect 推导，逻辑负载不变。

| 后端 / 轮次 | 内容变化下界 / 秒 | 观测变化间隔 P95 / ms | >50ms 间隔 |
|---|---:|---:|---:|
| MiniBlink A1 基线 | 59.5732 | 20.8762 | 0 |
| MiniBlink B2 候选 | 59.8892 | 20.7344 | 0 |
| MiniBlink A2 基线 | 56.8955 | 32.3817 | 见原始记录 |
| WebView2 A1 基线 | 59.8438 | 20.8884 | 0 |
| WebView2 B1 候选 | 59.7703 | 21.0029 | 0 |
| WebView2 B2 候选 | 59.9030 | 20.7302 | 0 |
| WebView2 A2 基线 | 59.9329 | 20.8020 | 0 |

MiniBlink A1/B2 的 +0.3160 次/秒、P95 -0.1418ms 都在冻结噪声 4.4 次/秒、1.1836ms 内；A2 P95 相对 A1 增加 11.5055ms，超过原噪声门槛，整组结论为 **不确定：基线漂移**。B1 因窗口位置不匹配被配对读者拒绝，保留为独立探索记录，没有纳入收益计算。此组候选绑定较早的 `59ff1431…` 本地快照；后来恢复的 WebView2 守卫未改变 MiniBlink 产品代码，仍不把旧组改挂为新载荷。

WebView2 使用同一固定 Runtime 156.0.4314.58，浏览器 EXE SHA-256 `8b6fabdf9f4ebb331cb7325bb9947e4e3f9a2b5345602feeacf277b1f9f355be`。四轮匹配，均自然退出、所属 Job 清空；A1→A2 漂移在冻结噪声 2.9922 次/秒、5.2970ms 内。候选变化也均在噪声内，结论为 **未检测到超过噪声的改善或退化**。这不证明等价，也不覆盖高刷新率。

资源采样按同一原生 QPC 观察窗裁剪。WebView2 A1/B2 的已观察进程 cohort CPU 约为整机 1.508% / 1.603%，private bytes P95 为 240,349,184 / 230,633,472 B，working set P95 为 447,524,864 / 447,119,360 B。它们不是整个进程树完整生命周期、共享浏览器或 GPU 内存总量；不据此宣称资源收益。

## 输入响应：失败和可用的有限诊断

预注册为 125Hz pointer_move，基线/候选/候选/基线各 3,750 次；另有候选 1,250 次 full trace，只作阶段定位，不计入 E2E 分数。输入守恒、前台、几何、期限、来源完整性与自然退出门槛保持不变。

基线 A1 实际发送 3,750/3,750，注入进程退出 0，Host 正常关闭但以 2 退出。SDK scalar 可用；MiniBlink kernel API 实际加载，初始 `attempted=0, unregistered_writes=0`，最终 `attempted=104754, unregistered_writes=2`；`dropped=0, causal_lost=0`、已停止且静默。原判定 `source_loss` 正确，**本轮验收失败**。现有起止快照不能确定两次写入发生在线程启动、运行还是关闭；不将其解释成仅关闭噪声。未放宽规则、修改旧内核或旧收据；停止剩余重复配对。

候选 10 秒 full trace 的 1,250 个输入均映射到 host/admission/engine/handler 阶段；“Host 收到→Engine 开始”的 P50/P95/P99 为 0.2524/0.7185/1.0014ms。428 个软件可见样本的 SendInput 调用区间→BitBlt 捕获结束上界 P95 为 37.2117ms，P99 44.6053ms（样本不足，P99 仅探索值）。其余 822 个输入无法逐个归类；阶段 7/8、微任务和逐帧原生完整链未证明，`fullCausalComplete=false`。因此这不是已通过的输入延迟、丢输入结论或性能收益。

读者原来错误要求 GDI warmup/ready 总携带 DXGI `lastPresentQpc:null`；生产者在这两个事件中省略该不可观测字段。修正只允许 GDI warmup/ready 的省略或 null，拒绝非 null；真实 frame 仍必须显式 null，其余格式/期限/来源门槛不变。修正后对原始候选记录回溯分析通过；没有重新运行原生采样或修改失败原件。

后续修复应定位基线内核未注册 producer 的实际来源，分别验证开始/运行/关闭边界，保留缺失即拒绝的契约。不能仅改 Host 退出码或把 source loss 忽略。若另做无诊断的 E2E 对照，必须作为新测量设计，两边相同 instrumentation，并重新冻结身份和阈值。

## WebView2 输出准备诊断

同一个最终消费者、SDK、页面及固定 Runtime，仅改变 `SOUI_WEBVIEW2_PREPARE_OUTPUT=0/1`；两侧计划使用 scalar trace、output timing 开启、Bridge phase 诊断关闭、Bridge echo / 4KiB / 30 秒。仓库外控制器先后暴露 SDK identity 字段映射、启动变量/工具自散列、缺失 import 与结果目录契约错误，失败均保留，没有改 SDK 以迁就控制器。

修正后的 r5 off 侧实际完成 30 秒，页面显示 38,784 次成功、0 失败、0 待结以及 DOM/原生快照完成；但保存和正常关闭之前触发 120 秒总监督期限，Host 由其自有 Job 终止，退出 124，整轮 **失败**。未执行 on 侧，也没有有效配对。没有延长期限或重复到有利结果；已保存的 scalar 边界仅可作有限离线诊断，不能继承为正常关闭或性能通过。默认仍关闭，owner 2ms/16，不据此新增队列、改变公平性或宣称准备成本已经优化。

这两个有效 scalar 边界间隔约 30.0049 秒；聚合桶的 P95 上界为 task_queue 0.4095ms、webview2_owner 0.2047ms、serialization 0.0255ms。task_posted/task_begin/task_end/bridge_response 各增加 38,785。这些是进程累计差值和量化桶上界，不等于页面成功次数、逐请求 COM 成本、输入公平性或正常关闭证据；失败整轮仍失败。

## 任务状态与默认策略

| 任务 | 最终决定 / 验证范围 |
|---|---|
| P00 | 保护原 dirty 工作区与 index；独立分支基于远端 e561，保留主线和后续修复。 |
| M01 | SDK 收据/安装头库/Loader 绑定，显式 GDI 软件观察及有界重复指针处理已实现；定向合同通过。输入来源完整性失败如实保留。 |
| I01 | 既有 publication receipt 驱动 pending 唤醒优化保留，FIFO、终态和恢复规则不变；局部正确性通过，端到端收益未验收。 |
| F01 | 既有 requested/effective/applied 协商保留；新增 provider 重试截止从操作及失败读回完成时起算（ADR0161），合成慢调用与 generation 回归通过。 |
| W01 | 既有不可变接纳 metadata 复用及字段短路语义保留；双架构受影响构建/定向合同有记录，实际宏观收益未证明。 |
| I02 | 不改变默认 shared budget=0；既有三档证据不足，2000µs 超时原件保留。 |
| W02 | PREPARE_OUTPUT 默认关闭，owner 2ms/16；本轮工具/退出失败，未形成有效 off/on 配对。不授予默认启用或新增机制资格。 |
| I03 | 不新增排队或线程。候选已见 Host→Engine P95 <1ms，但 provider 前缀及长回调完整占比未知；不将缺样本当成无热点。 |
| F02 | 普通 opaque DIRECT_HWND 且无 DC/readback consumer 的范围有限不采用额外两槽同步；特殊路径复制占比、damage 面积与像素等价仍未知，不作泛化。 |
| F03 | 保留已有诊断和 GPU/CPU fallback；不新增 phase-lock 或 present owner，缺少错相、transfer/present 主导及有效 CPU 副本证据。 |
| R01 | 交付源码、ADR、测试、体积结果和失败报告；性能/生产资格不通过，未把编译或工具通过提升为性能通过。 |

## 已执行的最小验证

- 官方 v143 x64 SDK-only 安装与分别链接的两版普通消费者构建通过；性能消费者 40 项原生合同、两个 InputUi ProfileContracts 均通过并自然退出。
- WebView2 output timing fixture v143 / SDK19041 x64 两项 CTest 通过。
- 当前集成树 190 项选定 Node 合同通过、0 skip；随后两条 GDI warmup 读者的 28 项、21 项合同分别再次通过、0 skip。既有 Web 客户端 31 项、包 smoke 16 项及受影响原生定向 37 项按各自源收据保留，没有重复全量构建。
- SDK Web 锁文件 audit 为 0 已知漏洞；不代表整个仓库或上游依赖无漏洞。
- 最终 WebView2 窗口实际拼音 `ni`、空格确认得到“你”，收到 `compositionend`，正常关闭。最终 MiniBlink 窗口也观察到拼音候选列表、空格提交“你”、内区滚轮从第 1 行滚至第 8 行，Host 退出 0 且 Job 清空；没有单独捕获该轮 MiniBlink 的 `compositionend`。两项均为短功能观察，不属于性能窗口或完整 IME 矩阵，不是物理键盘延迟测量。
- `git diff --check` 通过。完整 `UnitOnly`、最终全矩阵、长期运行、光学输入/显示、高刷、DPI/多屏不授予通过。

## 体积、交付与回退

实际生成并校验四个 x64 runtime 及四份 ZIP；同后端两边文件布局相同，MiniBlink 各 267 文件、WebView2 各 261 文件，包含相同页面资源及授权闭集。使用同一 7-Zip 和 `-tzip -mx=5 -mmt=2 -mcu=on` 参数，各 ZIP 完整性测试退出 0，逐项文件闭集已核。

| x64 Release / 项目 | 基线 / B | 候选 / B | 增量 / B |
|---|---:|---:|---:|
| SDK 安装前缀（471 文件） | 79,853,600 | 80,244,375 | +390,775（0.49%） |
| 普通消费者 EXE | 1,569,792 | 1,603,584 | +33,792 |
| MiniBlink node.dll | 39,204,352 | 39,211,008 | +6,656 |
| MiniBlink 完整应用 runtime | 46,866,217 | 46,906,665 | +40,448（0.08631%） |
| MiniBlink ZIP | 17,013,725 | 17,033,326 | +19,601（0.11521%） |
| WebView2 应用 runtime | 6,152,443 | 6,186,235 | +33,792（0.54925%） |
| WebView2 ZIP | 2,613,048 | 2,629,355 | +16,307（0.62406%） |

WebView2 表中的应用目录不携带系统共享或单独安装的 WebView2 Runtime；测量使用的固定 Runtime 本身 918 文件 / 913,129,028 B，不含在两边应用 ZIP 内，两端相同。不得将此 6MB 应用目录与内嵌内核的 MiniBlink 目录直接比较总部署成本。WebView2 运行依赖总量不变；MiniBlink DLL 合计增量为 +6,656 B，没有新增 DLL。大小读取器的 `kernelBytes` 是所配置的 EXE/DLL 闭集合计，并非单个内核 DLL 大小，本表已分别列出。

以上不引用旧 270 文件布局结果，也不是 x86 结论；x86 本轮未做同口径最终消费者与整包配对，保持未知。

没有替换已发布安装或发布同版本新 SDK。回退本轮 SDK 行为修正时撤回 provider completion clock 补丁及 ADR0161；消费者/观察器修复可独立回退，不修改产品默认设置。回退核心 I01/F01/W01 时必须配套版本、头库和默认内核，不能只替换单个不匹配 DLL。

本报告的完整原始证据在执行机器本地保留；[仓库证据索引](../../verification/performance/sdk-input-frame-20261105/summary.json)记录来源标签、运行 ID、散列和限制，不提交个人路径或大体积原始跟踪。最终源码清单在文档和证据暂存后由现有工具生成并校验。公开性能或生产声明仍须关闭上述适用门槛。
