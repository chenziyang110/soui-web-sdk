# SOUI Web SDK 追加验收与重载修复

结论：**MiniBlink 性能消费者的重载误关窗已修复并完成限定复验；整体性能验收仍未通过。** 本轮没有修改、重编或重新打包 SDK/内核，没有运行 Electron，也没有改变调度或输出准备默认值。物理 FPS、完整输入到可见反馈和配对资源成本仍未认证。

本报告接续[条件优化与输入诊断](sdk-preview4-adoption-20261106.md)，保留前一检查点及其失败。机器记录见[continuation-r28.json](../../verification/performance/sdk-input-frame-20261105/continuation-r28.json)。原始证据路径以下均相对于本地验收归档 `PR129-performance-20261104/`；不是源码目录内的构建输出。

## 对象与修复

- SDK 为冻结的 `0.7.0-preview.4`、x64 Release，build ID `18e47fa2-69df-4530-b1f9-365f6b2aec28`，SDK source SHA `e1d2dd8b7699930f678e21a77772b8fa4e371f5b67a53937d98470ee1659e87c`。
- 原性能消费者 FA578、MiniBlink DLL 56d460及旧数据保持原身份。新 R28 消费者只修重载调用方；其 main SHA 为 `aebaa20590c2b7ec8939948a07ae00db3326e48825e376af6a03133efb19c2e7`，EXE SHA 为 `0b4c297f1eb9a89db99a6a8190a4db1a266941045c34111ff626ca0e2e813e0e`。它不继承旧消费者的性能资格。
- R27 标准页实际操作发现：`E_RELOAD_PENDING` 被消费者当作终态错误，随后关闭 Host。[公共契约](../api.md)要求保留 Host/adapter，在同一 UI owner 后续轮次继续取得原操作结果；此次不能判定为 SDK reload 失败。
- 修复只对精确 pending 续调，首次调用紧邻原动作许可领取，后续不重复成功操作。关闭取消续调，在当前回调退栈前保留 Host。新增从动作开始计算的固定20秒合作式 caller 期限，在调用前后检查，不能抢占单次调用；其他错误仍失败，原关闭、writer 和外层期限不重置。返回成功与新页面 ready 分别验证。

## 实际执行结果

| 项目 | 实际结果 | 判定边界 |
|---|---|---|
| R28 局部构建 | MSVC v143 14.44.35207 / SDK 19041，单消费者 TU，61.716秒，退出0 | SDK/内核未重编 |
| R28 无窗口合同 | 原42项及7项共用 reload helper 反例，共49项，退出0 | 包括 pending、成功一次、错误、关闭取消、重入、固定超时、异常退栈 |
| R28 定向 GUI | 原生 Host/监督器/驱动均0，220.244秒，Job0，无强制清理 | 两窗正常关闭，Runtime shutdown 成功 |
| 重载身份 | 同一 Host0，page/native sessionGeneration 均由2变3 | reload 仅一次完成；新文档及应用 ready，新报告实际保存 |
| 新会话输入 | keydown1、keyup1、input2；实际按键后可见文本 | composition start/update/end 各1仅是本次事件计数，不认证完整中文 IME |
| 焦点 | 实际切换两个任务 Host，报告没有 `window_blur` | 保留疑点；没有逐动作时间/原生消息证据，不归因为已确认的内核缺陷 |

旧报告为6.46207秒 `manual_stop`；新报告实际已满30秒、软件 `valid=true`。该 valid 不证明原生前台稳定、全部焦点路由或性能通过。新页面输入与会话更新结合实际重连、持久化和正常关闭，才构成此次重载限定通过。

R27 旧失败保留：Host/监督器3、驱动1，268.952秒，Job0，正常关闭两窗及 Runtime。旧报告分别只覆盖键盘、scroll16、第二 Host 短记录；拖动发生在首轮记录结束后，wheel计数为0，不能把 GUI 观察补入报告或当完整交互矩阵通过。

## 新增性能诊断及未通过项

WebView2 输出准备 off/on 用相同冻结消费者、SDK、页面和 Fixed Runtime156.0.4314.58，各完成32次预热及1024次4 KiB/最多8在途调用，Host/helper 均0、Jobs0。两侧序列化 P95 桶均为 `[0.0128,0.0255]` ms，owner P95 均为 `[0.0512,0.1023]` ms；task queue P95 分别为 `[0.2048,0.4095]`、`[0.1024,0.2047]` ms。只有两个短聚合样本，没有噪声基线、持续 owner backlog、prepared/fallback 字节或线程分布，**不能确认因果收益或公平性**；默认 `PREPARE_OUTPUT=0`。

I02 后续探索取得0/0/1000前缀。1000档 Bridge P95 为5.115 ms，超过事先冻结的3.5145 ms筛选界限，拒绝原样保留；这不是因果退化证明。2000档及随后一次独立新序列的首基线均在编译占用准入阶段停止，未产生新数据。末轮对照缺失，默认预算0，I02仍未完成。

R26 输入诊断只完成测试工具的局部构建和62项无窗口合同。首轮交接用了不合法的窗口读取参数，未发 GO/输入；失败保留。修正参数后的唯一补轮接纳 GO，发送2527/3750后失败：原生 probe fault 先触发关闭，随后原20秒关闭期限耗尽；输入器另报实际 child 命中检查及观察收尾失败。Host3/Input1/outer1，Jobs0、无外部强制清理；缺失首故障账本、正常 SDK shutdown 和最终导出。读取器的缺文件错误是次因。**首故障原因、完整输入链、输入尾延迟均 unknown**；本轮不再补采或改期限。

既有单屏3750诊断的 input_queue P95/P99桶上界为0.8191/1.6383 ms，provider机会 P95/P99为0.0063/0.4095 ms；这些是全生命周期聚合，不能代替逐输入前缀或 input-to-photon。既有两后端桌面观察仍因噪声/漂移未确认收益，软件变化频率与 rAF 均不是物理 FPS。

## 任务采用状态与体积

| 任务 | 当前状态 |
|---|---|
| P00 | 源码身份、旧改动保护和原 ADR/所有权沿用 |
| M01 | 多项工具修复已验证；完整输入/可见链仍未合格 |
| I01/F01/W01 | 已实现及限定正确性验证，性能收益未认证 |
| I02 | 探索不完整，保持预算0 |
| W02 | 短负载未建立默认开启动机，持续 owner 公平性未验 |
| I03 | 轻/重载聚合成本未建立新增机制动机，精确输入前缀与长回调归因仍开放 |
| F02 | stock opaque/DIRECT_HWND 重载复制计数0，不新增 damage-history；特殊路径不外推 |
| F03 | 本轮不采用未编译的 promotion 模型，保留已有源码风险与决策证据 |
| R01 | 限定构建、消费者、体积和本次重载通过；性能/完整交互矩阵未闭合 |

冻结的同源 x64 产品体积继续沿用：EXE +34,304 B，MiniBlink DLL +7,680 B，MiniBlink runtime/ZIP +41,984/+19,502 B，WebView2应用 runtime/ZIP +34,304/+16,636 B。外置浏览器 Runtime 不计入，x86配对增量 unknown，严格零增长仍false。本轮未重压缩；R28 测试 EXE 为1,621,504 B，比原测试 EXE增加13,824 B，不能把旧runtime/ZIP结论挂到这个未重新打包的测试程序。

剩余门槛包括完整输入可见链、有效帧与物理显示边界、I02完整对照、W02持续公平性、配对CPU/完整内存、焦点及适用IME/拖放/DPI/多屏。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`；PR129保持draft，不合并、不发布或替换安装。

原始证据入口：

- `repair-r22-wv2-prepare-output-compare-r3/comparison-report.json`及独立实际对照审查。
- `repair-r27-standard-functional-r1/functional-result-r1.json`及独立功能/焦点审查。
- `repair-r26-wv2-input-attribution/native-sta-finish-r3-r2/run-once/independent-failure-closeout-r1.json`，含旧失败父链。
- `repair-r28-reload-continuation/attempt-r1/build-aggregate-r28.json`及实际构建独立审查。
- `repair-r28-reload-continuation/targeted-gui-r1/actual-functional-result-r1.json`、原报告、生命周期、owned记录及总控GUI观察。

回退只需撤销本次消费者修改；SDK/运行库及已有包不变。旧 FA578 及R27失败载荷保留可重现身份，但无需为了回退说明重复执行失败。
