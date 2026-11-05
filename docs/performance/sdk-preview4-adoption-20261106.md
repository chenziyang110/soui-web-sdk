# preview.4 条件优化决策与输入诊断（2026-11-06）

当前候选仍为 `0.7.0-preview.4`，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，x64 Release build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`。本轮追加测试工具修复、既有样本分析和条件优化决策，没有修改 SDK/内核或重打 SDK 包。`performanceVerified=false`、`releaseReady=false`，性能 Goal 尚未完成。

本记录接续[软件提交与原生阶段报告](sdk-preview4-lineage-native-phases-20261105.md)。历史结果和失败保留，证据路径相对于 `PR129-performance-20261104` 验收包；各项结论只覆盖其实际负载。

机器记录为 `verification/performance/sdk-input-frame-20261105/adoption-r24.json`，由只读收集器实际生成，绑定27项当前证据；诊断成功、采用决定、未完成性能门槛分别记录。

## 输入采样失败与工具修复

MiniBlink 重负载输入首轮在尚未开始注入时，既有详细导出器已达到 100,000 点上限，且没有收到该轮启动信号。该轮失败保留，没有提高容量或缩短 30 秒负载。后续改用既有 scalar 模式，并停止详细模式长轮次。

scalar 基线 `0-B0` 在已发送 1,008/3,750 个输入后因页面标记无效退出；Host/input 分别自然退出 2/1，Job 均清空。没有运行其余预算档位或冻结一个有效的策略噪声基线，不能据此选择默认调度预算。

随后仅增加有界 DOM 坐标旁证的一轮复现了具体异常：第 1,432 个页面回调收到 client `(101,100)`、对应测试序号 2，而此前页面已处理到 999；前两个回调为 `(99,100)`，在测试编码区域外。这些是页面回调编号和坐标，不能直接绑定为某个注入编号、丢包或 SDK 错序。原生接收坐标尚需旁证，根因未归属输入工具、SDK 或引擎。该轮 Host/input 仍自然退出 2/1、Job 清空，原失败没有改成通过。

另一次启动曾在 ready 文件存在但仍被原生独占写入时读取到 `EBUSY`，输入没有启动。正式测试器现允许 `ENOENT`/`EBUSY` 在原 30 秒 ready 截止内等待，不重置截止；权限错误、非法内容、held 进程身份不符仍立即拒绝。原生 writer、输入协议及性能门槛不变。针对性测试实际 15/15 通过、0 fail/skip；mock 覆盖共享冲突后成功、原截止不延期、其他错误及身份错误拒绝。这不表示修复后的原生轮次已实际触发共享冲突分支。

证据：`repair-r20-budget-input-consumer/scalar-i02-r1/runs/0-B0/analysis-r1/`、`coordinate-diagnostic-r2/run-once/analysis-r1/`、`repair-r24-ready-sharing-race/`。没有重新运行此前两后端的八轮帧观察、SDK 构建、前端构建或压缩。

同 R20 消费者只增加实际 SDK 子窗口的 UI 线程有界旁证后，运行一次完整诊断（run `0947b7a7fd5e379cd42d9be5e994db88`）：3,750/3,750 输入已发，页面单调观测3,750、拒绝0、同位置重复1,935；32次预热及固定1,024次4 KiB/8在途调用全部成功。Host/input 分别自然退出0、32.611/30.159秒，Job清空、无强清；原检查、SDK/kernel scalar终态和正常关闭通过。原生旁证在冻结前记录到5,690次callback，其中3,750次expected-magic-tag callback没有已记录坐标偏离，子窗口在WM_NCDESTROY正常解除旁证。

这次保留first4+last128原生记录，整个过程只保存总tag计数及逐条坐标比较，没有全历史tag去重/连续性账本，不能声称每个注入均已唯一关联至原生/DOM事件。此轮额外旁证没有修改事件或SDK，但有观测成本；仍是诊断，不能重标为正式性能基线。原DOM回退未复现，也未被修复或归因。前后显示配置相同，只有一个1920×1080、约75Hz路径，与旧故障的两路径3840×1080配置不同，不按原环境直接配对。原件在 `repair-r20-budget-input-consumer/same-consumer-native-witness-r1/diagnostic-r1/run-once/`。

只读分析该轮已有原件：SDK全生命周期 `input_queue` 共5,690样本，P95/P99桶上界为0.8191/1.6383 ms，不是3,750次注入的精确cohort。`engine_provider` 共12,191个机会，P95/P99桶上界为0.0063/0.4095 ms，包含没有实际工作的机会，不是逐输入前缀或CPU份额。整Host在begin/done快照间消耗约48.8486%单逻辑核等效CPU，包含观察器、Bridge、canvas和SDK工作，不能归为SDK孤立成本。Canvas callback约35.221 Hz、固定echo约33.509次/秒、DOM RTT P95/P99为3.10/6.99 ms；都不是实帧或物理输入延迟。

GDI共1,335次捕获，精确ACK推进1,215个（3,750的32.4%）、重复捕获120次；其余2,535个输入ID没有精确ACK捕获，保持unknown，不记丢包或合并，也不能从该子集生成总体输入尾分位。以上成本由 `current-costs-r25.json` 绑定9项证据；完整机器路径为 `verification/performance/sdk-input-frame-20261105/current-costs-r25.json`。

## 条件优化采用决策

| 项目 | 已执行依据 | 本轮决定及边界 |
|---|---|---|
| I02 共享预算 | 首个重负载基线因坐标异常无效；1000/2000 µs 尚未运行 | 保持默认 0；采用决策仍待有效的既定对照，不能把采样失败写成无收益 |
| I03 provider 顺序 | light full64 的1,128个机会及当前重负载12,191个机会均已有whole-life桶观察 | 当前样本未建立调整顺序的动机；不是同一输入前缀归因或CPU份额，逐输入前缀及其他长回调归因仍未知 |
| F02 damage history | 当前 stock opaque/DIRECT_HWND full64 内核记录 copy_operations=0、copy_bytes=0、内部 lease=911 | 本轮不新增该路径的 damage history；不推广到透明/DC、自定义适配器、长外部 lease 或所有负载的零复制 |
| F03 draw wake promotion | 真实源码片段的虚拟时钟反例可复现旧 delayed wake；新模型尚未编译，且有生命周期/采用范围缺口 | 本轮不采用 R21；保留现有默认及回退。不是认定真实样本主因，也不是实测 12 ms 收益 |
| W02 output preparation | 当前 WebView2 4 KiB/8 在途、32 预热+1024 成功的一轮短诊断 | 保持 PREPARE_OUTPUT=0；普通短负载没有建立新增准备工作的动机，持续 owner 公平性另待原生输入证据 |

当前重负载的已安装public counter enum与kernel来源逐项同序，初末及raw end核对得到 `copy_operations=0`、`copy_bytes=0`、内部 `buffer_leases=2773`，来源完整、无已记录丢失，未触发F02显著复制增长的重开条件。另一个 `raster_bytes=5667871056` 统计栅格工作，不能误作复制字节；同样不把内部lease次数解释为零成本。独立补件为 `repair-r24-ready-sharing-race/current-costs-r25-independent-review-addendum-r1.json`。旧light full64的911次lease仍属于原样本，不改写。

I02后续仅准备了原评分规则不变的五label策略探索：先两次0预算冻结噪声，再1000/2000及末次0检查漂移，不把上述诊断重标为正式基线。首次新 `0-B0` 在Host/Input启动前发现其他CMake/MSBuild/cl活动，准入helper自然退出1、2.587秒内Job清空；controller实际退出1，没有产生基线、噪声或任何候选预算数据。原失败保留，后续label没有执行，默认0及I02未完成状态不变。没有反复启动、停止其他任务或绕过准入，fresh入口只准备未运行。

F03 的具体风险包括：实验模型允许超出 ADR 即时任务范围的 future-earlier promotion；基线源码在异常分配或 admission/post 失败时已有条件性的 owner 唤醒恢复缺口，候选需要保留或补齐；候选新增的计数器及真实 Host/endpoint 关闭未接线验证。这里没有运行复现正常负载卡顿，不能把基线的异常缺口一并写成候选新增。已有虚拟反例是采用评估的依据，不能把未编译实验写成生产修复。

WebView2 短诊断的测量工作约 175.2 ms，不能当作 30 秒持续压力。owner serialization 共 1,057 个样本，P95/P99 桶分别为 `[0.0128,0.0255]`/`[0.0256,0.0511]` ms；whole owner pump 共 900 个，P95 `[0.1024,0.2047]` ms；output admission 到 dispatch 的 P95/P99 为 `[0.4096,0.8191]`/`[0.8192,1.6383]` ms。它们是 inclusive wall/聚合直方图，不是精确分位、CPU 份额、逐任务 backlog 或连续唤醒因果证明。Host/helper 自然退出 0，未混入 MiniBlink 阶段。

决策与独立审查：`repair-r18-reader-integration/f02-public-sdk-scope-independent-review-r1.json`、`light-provider-bounds.json`、`repair-r21-draw-promotion-prototype/F03-ADOPTION-DECISION-R1.json`、`independent-review-r1.json`、`repair-r22-wv2-output-trigger-r1/report.json`、`execution-independent-review-r1.json`。

## 原生 owner STA 对照的实际状态

为区分原生窗口接收与网页回调，外部消费者 R23 已用 MSVC v143 14.44.35207 / Windows SDK 10.0.19041.0 实际编译，42 项既有无窗口自测通过；二者自然退出 0、Job 清空，无强清。EXE 为 1,654,784 B、SHA `5e15b02f1f90b0d839d3f624c720e75ad316646e6c370f2e620c88e5be10bb2f`，不是正式应用体积对照载荷。

此前的 `attempt-r4` 实际配置因外部复制布局缺少三项 provenance 文件失败，未编译消费者或运行自测；PowerShell 包装退出 1、Job 清空，原 CMake 进程退出码没有保留，保持 unknown，不能用默认 1 替代。之后修正外部文件闭包，才执行上述 R5-r2 编译。该构建失败与下面 native R3 的编译占用准入拒绝分别保留。

外层 Node 汇总因报告路径重名 `EEXIST` 实际退出 1，完整构建流水线仍记失败。追加的离线读取只确认已经完成的真实编译和自测，没有重建、重测或改写旧失败；这项窄资格不包含窗口输入、marker、首故障旁证或性能。运行结果须单独追加，不继承构建成功为性能通过。

证据：`repair-r23-wv2-sta-probe/attempt-r5-r2/actual-build-closeout-r5-r2a.json`、`independent-actual-build-review-r5-r2a.json`。

现有 EXE 随后实际运行一次，run `ed2aa79a53ddbf6ec0a76a66e8e6f45d`。输入器在首次注入前的前台/尺寸/held 身份联合检查拒绝，未创建输入 trace；不能仅凭该组合错误认定其中哪一项变化。Host/input 自然退出 3/1，19.836 秒内 Job 均清空、无强清；32 次预热成功，测量请求和已接收输入均为 0，firstFaultInput 为 null。SDK scalar 终态完整、显示配置前后一致，但 `captureQualified=false`、`measurementQualified=false`。原始结果保留于 `repair-r23-wv2-sta-probe/native-sta-once-r1/run-once/`；没有自动重试或沿用该轮作输入/公平性结论。

原程序明确以 `SW_SHOWNOACTIVATE` 启动；后续只补操作者激活与绑定 GO 的衔接，保留 ready 15 秒、native 60+5 秒和原输入器全部检查。R2 的窗口激活成功，但总控工具往返超过原 ready 截止，未发布 GO；outer 自然退出 1、Job 清空。R3 在启动 Host 前发现其他编译活动，准入拒绝、outer 自然退出 1、Job 清空，没有输入或 GUI。外部活动结束后，R4 沿用逐字相同的五份控制源码，压缩总控往返，在原期限内完成激活和 GO；这些失败分别保留，没有扩期、取消准入检查或重编消费者。

R4（run `1d7465744e44b061975592eb96448c9a`）接纳的前五条 tagged 输入坐标正确。首个拒绝记录为 tag=0、client `(105,100)`，当时 expectedNext=6、last=`(104,100)`，原因是输入流中出现无效或零 tag；它不是接纳 receipt，tag=0 也不能认证事件来源。实际发送 69/3,750，未完成；不能据此认证跨行坐标、全部轨迹或归因 MiniBlink 页面异常。Host/input 自然退出 3/1、Job 清空，诊断时段 15.092 秒（外层 wrapper 18.726 秒）；`captureQualified=true` 仅代表捕获到了诊断旁证，`measurementQualified=false`。本轮只有32次预热、没有合格持续压力结果，不关闭 W02 或输入性能门槛。原件见 `repair-r23-wv2-sta-probe/native-sta-once-r4/run-once/`。

## 当前交付边界

I01/F01/W01 已有实现和针对性正确性证据；限定 x86/x64 SDK、消费者与匹配运行包资格沿用[既有交付记录](sdk-preview4-qualification-20261105.md)，没有本轮重复执行。已测 x64 完整 MiniBlink runtime 增长 41,984 B（0.089576%），ZIP 增长 19,502 B（0.114616%）；WebView2 应用 runtime 增长 34,304 B（0.557243%），ZIP 增长 16,636 B（0.636311%），外置浏览器 Runtime 不计入。旧严格零增长结果仍为 false，x86 配对体积仍 unknown。

有效输入/策略对照、完整输入到可见反馈、CPU/完整内存配对和适用交互验收仍未闭合。既有两后端帧观察受冻结噪声或基线漂移影响，不确认帧率收益；物理 FPS 和 input-to-photon 保持 unknown。现有成果可供开发评审，不代表所有性能目标完成、达到 Electron 水平或生产就绪。回退保持原版本 SDK、运行库、消费者与收据完整匹配。
