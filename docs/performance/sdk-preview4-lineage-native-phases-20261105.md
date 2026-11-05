# preview.4 软件提交关系与原生 Bridge 诊断

本轮修复测量读取器并复用既有原始样本完成分段分析，另执行一次当前载荷的固定 Bridge 诊断。**没有修改 SDK/内核、重建 SDK 或生成新 SDK 包；整体性能验收仍未通过。** 追加[上一份报告](sdk-preview4-miniblink-input-20261105.md)，保留旧结果和失败。证据路径相对 `PR129-performance-20261104` 验收包。

候选身份仍为 `0.7.0-preview.4`，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`，x64 Release build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`，默认 node.dll `56d460c0…`。本报告不重新认证大归档或沿用其他载荷的性能资格。

## 修复帧级事件的读取缺口

原读取器只按输入范围关联后续阶段，因此漏掉有意清空输入范围、改由 frame/scene 标识的提交记录。新增 `software_frame_lineage` 补充字段，严格核对同进程时钟域、完整代际、唯一 processed coverage、frame/scene 和时间顺序；缺失、重复、歧义、取消 handler 或时序冲突均拒绝关系。异步 resources_ready 可选，缺失不补零。

原始 stage8 不会被改写为继承输入范围。原 strict stages、输入分类、`fullCausalComplete` 和所有性能门槛保持。40 项构造 trace/协议合同直接退出0、40 pass/0 fail/0 skip；新读取器只对原 full64 数据重放一次，与独立分析逐行 QPC/frame 及完整分布相同。没有为此重新采样。

## 同一 full64 样本的真实可观测区间

64 个输入严格关联到42个软件帧。按每个输入或每个去重帧的原 QPC 先作差、再统计，未相减分位数。

| 软件区间 | 样本 | P50 ms | P95 ms | P99/最大 ms |
|---|---:|---:|---:|---:|
| SDK 接收→引擎开始 | 64输入 | 0.2377 | 0.9732 | 2.0373 |
| SDK 接收→软件提交调用返回边界 | 64输入 | 12.0823 | 24.8714 | 33.1723 |
| processed coverage→合成入口 | 42帧 | 3.9260 | 13.3021 | 24.0128 |
| 场景激活→提交调用返回边界 | 42帧 | 0.8026 | 1.1939 | 1.5535 |
| 合成调用 inclusive wall | 42帧 | 0.8445 | 1.4724 | 1.9471 |

输入共享帧，不能写成64个独立帧。processed coverage 只说明处理上下文被分配到帧；提交调用返回不证明 API 成功、对应像素已变化、GPU/DWM 完成或扫描输出。源代码预期普通 InputUiHost 请求60Hz，实际 applied getter未保存，不能套用另一组100Hz请求/75Hz显示的画面测试。

42个合成调用均用同 producer/thread 的实际调用栈及task执行栈唯一定位，没有按可能变化的 render-context task_id 硬配。15帧的实际执行任务早于 stage6 发布、但尚未执行；27帧在其后发布。没有原始 due/delay 字段，不能将 post→begin 称为调度超时，或据此认定延迟 DrawFrameTask、双重等待、GDI/vsync 是主因。延迟唤醒源码夹具用于独立验证这一假设，不代表已实施生产优化。

后续独立源码夹具已实际执行：保留真实 queue/DrawFrameTask/pacing 前缀，虚拟时钟下旧 delayed wake 令1.004秒就绪的请求等到1.016秒；无旧排队任务的控制在1.004秒执行，未paced控制仍限帧。唯一CPU运行及Job正常收口，LLVM22/MSVC142/19041的窄资格，不能称真实12ms收益或v143内核验收。原失败与续行日志单独保留，生产方案仍按[ADR-0163](../adr/0163-compositor-draw-wake-promotion-experiment.md)验证生命周期、绝对due和有界进度后才能采用。

GDI 原件21次采集含18个精确 ACK 推进、3个重复 ACK，其余46输入的精确反馈保持 unknown，不推为合并、丢失或不可见。18个已观测 ACK 从注入调用到该次捕获完成的上界为24.1342～52.5518ms，包含采集开销，不是首次 UI 反馈或物理延迟。最初分析漏看相邻同 capture 的 ACK0 raw marker；保留R1，以追加R2核对基线CRC，计数及全部差值未变。

## 当前 MiniBlink 4 KiB 原生阶段

复用相同14文件消费者及当前SDK4运行载荷，在私有非输入desktop仅运行一次诊断：32预热、1024固定成功调用、最多8在途；PHASES=1、SCRIPT=0。Host及helper自然退出0，Host Job清空，267项载荷前后字节不变。Begin/End缓存年龄102.203/51.4266ms。

| 原生阶段 | completed | inclusive均值 ms | P95桶 ms | P99桶 ms |
|---|---:|---:|---:|---:|
| 首次解析 | 1065 | 0.021646 | 0.0256–0.0511 | 0.0256–0.0511 |
| resolver创建JS | 1065 | 0.091549 | 0.1024–0.2047 | 0.2048–0.4095 |
| reply执行JS | 1065 | 0.091504 | 0.1024–0.2047 | 0.4096–0.8191 |

各阶段起止inFlight均0，包含预热及额外9次原生观察，不能称纯1024次echo的成本。inclusive区间可能嵌套，不相加为CPU份额；桶界不是精确分位。该自有诊断页不认证原trace-off固定UI，本轮没有基线、前台输入或FPS收益结论。当前4KiB数据未建立新增解析切片的动机；provider前缀、其他载荷与实际输入重叠仍需各自证据，不能全局关闭I03。

## 证据与未完成项

原始关系及独立审核：`repair-r16/full64-frame-lineage-r1/`、`full64-frame-lineage-r2/`、`full64-frame-lineage-r3/`、`full64-task-queue-context-r1/`、`full64-feedback-bounds-r2/`；官方读取器重放及测试：`repair-r18-reader-integration/`；当前原生阶段及独立审核：`repair-r18-native-phases/`；延迟唤醒源码夹具、保留失败及唯一CPU运行审核：`repair-r19-draw-wake-source-fixture/llvm-r3/`。机器记录为 `verification/performance/sdk-input-frame-20261105/software-lineage-r18.json`。

完整输入到可见链、物理FPS、完整CPU/内存归属及适用交互门槛仍未闭合。I02三预算、W02连续owner公平性、I03前缀/长回调、F03采用决策继续推进；F02默认opaque路径的限定结论不覆盖特殊复制路径。`performanceVerified=false`、`releaseReady=false`，没有合并或发布。
