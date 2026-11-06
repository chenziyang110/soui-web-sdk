# preview.4 MiniBlink 与输入诊断追加验收

两后端各四轮画面观察已完成，**尚未确认可重复的性能提升，输入延迟验收仍未通过**。MiniBlink 末轮基线漂移超出事先冻结的门槛；WebView2 结果见[前一报告](sdk-preview4-frame-observation-20261105.md)。`performanceVerified=false`、`releaseReady=false`，整个性能目标未完成。

本记录追加历史结果，不覆盖旧失败。产品仍为 `0.7.0-preview.4`，源码 SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，x64 Release build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`。没有重编 SDK、修改业务负载、生成同版本新包或运行 Electron。以下原始路径相对本地 `PR129-performance-20261104` 验收包。

## MiniBlink 四轮结果

两侧消费者使用相同 14 文件源码集 `53bad5158f3dc7290919717f3c93f0015ad9b6040544602d2e577ec46651d4cb`。基线为 SDK 0.6.0 发布内核 `d888637d…`，候选为 preview.4 默认内核 `56d460c0…`；没有将另一个 K0 trace 诊断变体冒充发布基线。

真实显示拓扑变化后，重新只读校准并冻结 DISPLAY1、1920×1080、`18559000/247500 Hz`（约 74.9859 Hz）、96 DPI/DPR 1。客户区为 1920×1009，完整画布 ROI `(402,425,480,360)`。测试页仅将 main 最大宽度 1180 改为 1181 px 以对齐整数像素，JavaScript、240 角色、种子、480×360 逻辑画布及 100 Hz 模拟负载不变，Host 请求仍为 100 Hz。

每轮先保存完整 30 秒页面记录，再重新运行固定场景并独立观察画布 30 秒；两个区间不混算。观察期间没有界面控制或截图调用。四轮 Host、观察器和资源采样器均自然退出 0、Job 清空；原始 reader 重放及身份校验通过。

| 轮次 | 采样到的画面变化下限 Hz | 变化间隔 P95 ms | >50 ms 间隔 | 捕获 P50 / P95 ms |
|---|---:|---:|---:|---:|
| A1 基线 | 74.606993 | 14.1258 | 0 | 12.6356 / 13.4919 |
| B1 候选 | 74.693416 | 14.3777 | 0 | 12.5278 / 13.6253 |
| B2 候选 | 73.072840 | 14.2798 | 0 | 12.6022 / 13.4719 |
| A2 基线 | 70.862992 | 25.2293 | 0 | 12.6449 / 13.8196 |

A1 后、任何候选启动前冻结的最小可辨差异为 **3.7303496 Hz / P95 1 ms**。A2 相对 A1 为 −3.7440009 Hz、P95 +11.1035 ms，超出漂移门槛，结论为 `inconclusive-baseline-drift`。B1 相对 A1 仅 +0.086423 Hz、P95 +0.2519 ms，未超过噪声；B2 对漂移后的 A2 的差值不能证明提升。不追加相同条件采样来寻找有利数字。

四轮先前页面 rAF 约 75 Hz、逻辑步均为 2999；这些是页面区间数据。GDI 捕获 P50 约 12.5–12.6 ms，实际采样间隔约 13.3 ms，存在遗漏转换及观察开销。表格不是精确唯一帧率、物理 FPS 或输入到光子延迟。WebView2 使用先前 DISPLAY5/100 Hz，不能用两组读数比较后端快慢。

## 输入坐标旁证

独立测试消费者完成候选 MiniBlink 的一次 64 次、125 Hz、`e2e/scalar` 诊断：原生 64 条带 tag 的原始坐标与请求逐一一致，页面包含全部 64 个不同编码坐标，未报告 invalid、拒绝或 overflow。20 张 GDI marker 的 CRC、内容计数及最终 ACK 64 已独立复核。额外无匹配 tag 的事件保留，tag0 不证明其来源，页面也不能逐事件认证原生 tag。

Host 和输入器的原始 owned 收据均为自然退出 0、Job 清空、无强清；scalar 来源可用。**外层 wrapper 仍失败退出 1**：它用 CREATE_NEW 写入已由原生 Host 创建的 `host-result.json`，触发 EEXIST。没有覆盖原结果、补写成功或仅为消除此收据失败重复输入。

本轮未复现旧 R15 异常，但它是带额外旁证的短诊断，不是正式延迟 A/B。详细 trace、1250/3750 次场景、来源完整因果链和物理输入延迟没有因此通过。另已定位旧正式 runner 在输入器非零时立即停止全部进程的工具路径，可能抢先截断 Host 原本正在进行的正常收尾；旧失败不追认通过，修复与针对性合同须分别记录。

为检验详细 trace 模式这一新假设，随后仅补了一次相同 64 次/125 Hz 的 `full` 短诊断，没有扩容旁证或重编 SDK。原生 Host、输入器和独立 driver 均实际退出 0，坐标旁证报告完整、未观察到偏离，详细 trace 已导出；`records-exported-links-unqualified` 仍不能自动升级为完整因果或延迟通过。该模式按原设计不生成 scalar 初末导出，不能把 `scalar_usable=false` 误判为本轮 scalar 来源损坏。新结果文件名区分原生 `host-result.json` 与 `host-owned-result.json`，旧 EEXIST 失败保留。

离线原 reader 校验 SDK/kernel 的 18,119 个详细记录，来源守恒、已记录 loss/fault 均为 0。按同 run、进程、QPC 频率、view/document/geometry 代际和原生输入 ID 关联到 64 个 **T1 `host_receive` → T3 `engine_begin`**：P50 **0.2377 ms**、P95 **0.9732 ms**、P99/最大值 **2.0373 ms**。这是当前轻载、开启详细 trace 的短诊断分布，不是优化前后差值，也不包含 T1 之前的系统输入投递或 T3 之后的绘制/显示；64 样本的 P99 不代表总体尾部资格。

T1/T2/T3/handler 及阶段6各关联64个输入；microtask、阶段7/8及 terminal 均没有对应证据，完整七阶段路径为0/64。来源 eligible 不代表完整输入到可见链通过。另有 SDK 整个生命周期的 `input_queue` 119 个桶样本，P95/P99 桶上界为1.6383/3.2767 ms；它包含未标记及启动包，不是上述64个事件的精确排队时间。T2 在 route 返回后记接纳，不是 enqueue 时间，不能以 T2→T3 或分位数相减替代排队测量。

正式 runner 已修复为仅向被持有的监督器请求停止：输入失败保留非零，同时等待 Host 的原期限收尾；Host 失败停止仍运行的输入器。完成通道拒绝和停止请求异常记录为缺失证据/失败，不生成通过。最终源码的 11 项编排合同实际退出 0，11 pass、0 fail、0 skip，覆盖原 watchdog、两类 ready 失败、完成拒绝、stop 异常及正常路径。这些是 mock 编排合同；本次 full 短诊断使用独立 driver，不能冒称动态运行过修复后的正式 runner。

## 保留的门槛与体积

完整应用 CPU、全生命周期和外部/共享进程归属、完整内存及 GPU 内存仍未知。轮询到的进程群统计不能扩大为完整应用资源通过。键盘/IME、按钮边沿、滚动拖动、焦点、导航、多 Host、DPI/跨屏仍需适用行为证据；当前鼠标短诊断不能外推。

沿用[既有同源 x64 体积测量](sdk-preview4-qualification-20261105.md)：EXE +34,304 B，node.dll +7,680 B，MiniBlink runtime +41,984 B（0.089576%）/ ZIP +19,502 B（0.114616%）；WebView2 应用 runtime +34,304 B（0.557243%）/ ZIP +16,636 B（0.636311%），不含外部浏览器 Runtime。未重复压缩。x86 缺匹配整包基线；旧严格零增长判定仍 false。

I02/W02/I03/F03 的条件采用证据、F02 特殊复制路径及最终输入尾延迟仍未闭合。现有源码可供开发定位与评审，不能据本报告宣称性能目标完成、达到 Electron 水平或生产就绪。

下一步先核查当前 DIRECT_HWND 路径的阶段7/8和 terminal：区分可选边界、关联工具遗漏与真正缺少的观测，不把记录为0解释成输入未处理。补齐相应身份/完成边界后，再按新冻结请求进行必要的正式输入对照。原发布 K0 的 source-loss 资格失败不能豁免，也不能把另一个诊断内核重标为发布基线；旧100 ms观察误差参数不能支持毫秒级收益结论。不重复已完成的画面四轮、SDK 构建或压缩。

原始证据：`repair-r16/analysis-miniblink75/`、`repair-r16/mini-entry-monitor75-r1/sessions/miniblink-{A1,B1,B2,A2}/`、`repair-r16/input-coordinate-witness/diagnostic-64-r2/analysis/`、`repair-r16/input-diagnostic-review-r3.json`。紧凑机器记录为 `verification/performance/sdk-input-frame-20261105/miniblink-input-r17.json`；保留之前中断、半像素 ROI 拒绝、显示不符及 R15 输入失败。
