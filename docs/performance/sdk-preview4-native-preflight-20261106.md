# SOUI Web SDK：原生输入入口复验与当前验收结论

**当前结论：实现和限定正确性检查可供开发评审，真实输入延迟与 MiniBlink 有效帧率仍未验收通过。** 本轮排除了窗口工具和分步授权的依赖，但原生测试器在首次发送前拒绝了目标窗口。没有发送预定输入，没有开始画面标记采集，也没有新性能收益结论。

本报告接续[同步输入旁证检查](sdk-preview4-sync-witness-20261106.md)。机器记录见[continuation-r36.json](../../verification/performance/sdk-input-frame-20261105/continuation-r36.json)。以下路径相对于本地归档 `PR129-performance-20261104/repair-r35-sync-input-witness-r1/`；旧入口、原始失败及已审收据均保留。

## 新入口与实际结果

冻结的原生测试器本来就会恢复自己的任务窗口，核对被持有的 PID/birth、同一交互 session、非 RDP、唯一窗口/class、1100×720 内容区、实际前台、几何和 SDK child 命中，并在首个 pointer SendInput 前等待本 run 的 nonce/CRC/零位图。重复声明窗口工具已激活目标没有提供额外机器证据。

新入口只将 GO 改成明确的任务范围原生输入授权：`soui.r35-native-input-go/v1`，绑定实际 run/request SHA/PID/birth/HWND、固定输入器 SHA 和 `frozen-input-tool-restore-and-verify` 策略。它不声称窗口已观察、已激活或机器独占；原生检查和严格读取器全部保留。前台检查不是与 SendInput 的原子锁，中途失败可能已经有部分输入，必须保留真实计数与非零。

首份新入口 `entry-r3-native-go` 的 run `4685eaf2fc991140bcb86513df0101dc` 未能及时发布 GO；helper按原30秒截止拒绝，Host2/入口1、36.527秒自然关闭、Job0、无强制结束，未启动输入器。随后只复用相同源码和产物，在一次 Root 工具调用中串行衔接启动、精确 phase 核验和显式授权，保留原截止，没有让入口自动产生 GO。

最终原件在 `recovery-single-bootstrap-r2/entry-r4-native-go/run-once/`：

| 项目 | 实际结果 |
|---|---|
| run | `2b149ce5ae51d51a218b21bc082a671a` |
| 冻结请求 SHA | `c26514cc20f04f2fe99d8f90f2130caaf02c42e1532aa8f8883d4d2a28ffc274` |
| GO | 启动后9.878秒接纳，原30秒截止尚余20.122秒 |
| Host | PID42224/birth`134384141204721111`，自然退出2，150.578秒 |
| 输入器 | PID33492/birth`134384141303387191`，自然退出1，66毫秒 |
| 原始错误 | `Target foreground, geometry or held process changed` |
| 预定输入/画面标记 | 0次；observer与nonce/CRC/零位图基线尚未启动 |
| 收尾 | 两个Job最终0进程、无强制结束、正常Runtime关闭；入口退出1 |

输入器 stderr、冻结源码顺序及两个 burst 输出文件缺失共同定位到首次 `validate_target`：此处位于 observer、输入输出创建和首个 pointer SendInput 之前。具体失败的是前台、几何还是进程条件仍为 **unknown**，不能拆分推断，更不能据此判定 SDK 卡死或产品回归。没有预定 burst 不表示所有正常 UI/SDK 输入事件均为0。

冷初始化仍实际通过：唯一 bootstrap、setup16、receipt4、cookie/owner及上下文核验成立，getter返回0，精确对应实际 SDK task9/point12。SDK/vendor详细trace分别17,875/83,944点，完整、loss0、fault为空且身份一致。冷 point12不等于由正输入触发的独立延后负对照；正输入ID、首次反馈槽与桌面捕获仍无实证。

输入器的早期异常发生在其正常结束事件之前。既有控制器让Host遵守原150秒活动边界、190秒Host与195秒总期限自然收尾，没有伪造结束事件成功、缩短产品清理或放宽错误。本轮没有修改或重编SDK、内核、消费者、输入器、页面或读取器。

事前独立审查 SHA `4a2bdf1acc00a9e973e021ee95573418f9e34815d3acc5b860c9770b6e5e645d`；本次实际独立收据 `actual-native-preflight-review-r1.json` SHA `0a6e48cde133a71c2cb150df84a796a1043350a5f38af69ed73d9a30adfcb513`。它保留 Host2/Input1/入口1，不把完整 trace 或自然关闭转成性能通过。

## 完整目标的当前状态

| 任务 | 实现或采用状态 | 仍缺的资格 |
|---|---|---|
| P00 | 已有源码身份、改动保护、所有权与ADR | 不代表其余验收完成 |
| I01/F01/W01 | 已实现并有对应正确性证据 | 同负载性能收益未认证 |
| M01 | 测量版本、读取器、生命周期与单次会话修复已执行；新同步原型仍独立、未默认采用 | 正输入→DOM ID→反馈槽→有效桌面捕获 |
| I02 | 保持共享预算0；0/0噪声门失败，停止追测 | 有效参数对照未完成，不能声称2000已获验证 |
| W02 | 固定4KiB/8并行短检查的准备成本较小，保持输出准备关闭 | 持续owner负载与公平性未认证 |
| I03 | 已记录轻/重负载provider桶成本；当前证据不支持重排 | 精确同输入prefix及长回调归因仍未知 |
| F02 | 已测stock opaque/DIRECT_HWND复制计数为0，未增加damage history | 不能外推透明、DC或外部长期lease路径 |
| F03 | 未采用R21，保留已记录的语义、唤醒及生命周期风险 | 没有新的Windows收益资格 |
| R01 | 有冻结构建、消费者、限定交互和体积证据 | 性能、资源及适用DPI/多屏等门槛仍未关闭 |

各决策的实际范围及原始限制见[条件采用报告](sdk-preview4-adoption-20261106.md)、[焦点与预算报告](sdk-preview4-focus-20261106.md)及[交付复验](sdk-preview4-delivery-20261105.md)。这些“不采用/保持默认”的限定决定不意味着所有条件任务均已完整完成。

## 体积、交接与恢复条件

本轮产品载荷没有变化；以下沿用同一冻结preview.4的已执行同源x64 Release体积对照，不是重测的新包。相对0.6.0：Host EXE +34,304 B、MiniBlink DLL +7,680 B；MiniBlink完整应用runtime/ZIP分别+41,984/+19,502 B，WebView2应用runtime/ZIP分别+34,304/+16,636 B，没有新增DLL。WebView2外置浏览器Runtime未计入，x86没有同口径应用体积对照。历史严格零增长比较仍失败，没有改阈值。

开发可按归档的 `handoff-r1/README.md`、`integration-plan-r1.md` 和 `positive-analysis-plan-r1.md` 复核实现与剩余门槛。同步输入原型依赖匹配的私有诊断头，不应直接覆盖默认基准CMake或删除legacy/ProfileContracts；优先保留独立提案，待正输入链和测量成本通过再决定是否集成。

恢复采样需要实际可操作的物理桌面，使本任务SOUI窗口满足前台、固定几何和命中检查。既有任务操作授权有效，不需要重复申请权限；授权本身不能替代这些机器条件。当前停止相同条件下的新启动，保留已编译载荷；恢复后仅做一次有明确条件变化的受影响采样，不重编或重跑无关矩阵。不能用虚拟机功能结果代替物理性能。

本轮提交前npm审计在TLS建立前断开，退出1，未取得当前审计通过结论；依赖没有变化。`performanceVerified=false`、`inputLatencyVerified=false`、`releaseReady=false`、`wholeGoalComplete=false`。PR129继续draft，不合并、不发布。
