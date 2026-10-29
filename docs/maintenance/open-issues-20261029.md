# 未关闭 Issue 逐项收尾，2026-10-29

本轮从主线 `0cdc0a9` 核查八个开放 Issue 的实现、评论与已有证据。维护者要求只补必要短测，输入未变且已通过的结果不重复，长测后置。源码已实现、限定场景通过和完整资格通过分别记录；不修改已发布 v0.6.0 的资产或历史失败。

| Issue | 已完成或可复用范围 | 剩余必要工作 |
|---|---|---|
| [23：生产能力与采用价值](https://github.com/chenziyang110/soui-web-sdk/issues/23) | 当前两架构构建、安装消费者、默认应用八次运行及归档闭集见 [0.6.0 实际记录](../../verification/production-validation/0.6.0-release-2026-10-29/RELEASE-FACTS.json) | 汇总下列专项、完整独立交付及 Collector、目标系统/资源/性能与采用价值。千次和八小时按后置范围执行 |
| [27：公共托盘](https://github.com/chenziyang110/soui-web-sdk/issues/27) | 公共服务、独立安装消费者和原故障边界已实现；当前安装 SDK 的独立 x64/x86 消费者在 Server 2022 VM 完成真实 Explorer 重启恢复、显式移除后不复活及自然退出 | 菜单退出焦点归还、其它故障/所有权边界与 Win7；旧 fake-platform 及 Shell 失败保留 |
| [29：菜单、快捷键与驻留](https://github.com/chenziyang110/soui-web-sdk/issues/29) | 公共模块已实现；x64/x86 有限三窗、重载、无窗驻留及最终退出短测通过；公共托盘另完成本 VM 的真实恢复及移除后不复活 | 菜单/快捷键完整成功返回、三窗业务隔离、模态关闭及 Win7；焦点失败、原前台检查与期限保持 |
| [31：同文档续期](https://github.com/chenziyang110/soui-web-sdk/issues/31) | 原生续期、稳定资源归属、Web 单飞续期及独立容量通道已实现，原契约/安装消费者结果保留 | 浏览器双层满载、任务/订阅/目录/媒体保留及 Win7；真实超过 30 分钟和八小时后置，模拟时钟不替代 |
| [39：依赖许可与链接成员](https://github.com/chenziyang110/soui-web-sdk/issues/39) | 首方 MIT 决定及随包原许可已记录；已有 [0.6.0 精确 DLL/MAP 来源与对象成员记录](../../verification/production-validation/current-link-members-0.6.0-2026-10-29/README.md)及 [24 个组件工程清单](../../verification/production-validation/current-license-crosswalk-0.6.0-2026-10-29/README.md)；已发布 0.6.0 的 1,238 份工程材料组合核验通过，版本冲突及未知身份保留 | 完整条款/嵌套与文件级审核、来源冲突补证、责任人与维护者审批；候选源文件和相同许可文本不等于完整传递依赖或法律批准 |
| [42：Win7 工作区与交互](https://github.com/chenziyang110/soui-web-sdk/issues/42) | 窗口定位代码早已修复；[Win7 V11](../../verification/production-validation/win7-v11-native-resize-2026-09-29/README.md) 指定历史构建已实际完成 80×60 缩放，夹具请求关闭后原生退出 0，旧文档的“缩放待验”已纠正 | 原生滚轮、中文 IME、公共任务并行和完整 V02 仍缺；已通过的指定构建缩放不重做，也不改挂到 0.6.0 |
| [77：MessagePort fixture](https://github.com/chenziyang110/soui-web-sdk/issues/77) | `caseId.maxLength=24`、`status.maxLength=11` 已合入；当前 SDK 两架构候选各完成四文档、32 行为及自然退出 | x64 基线完成 32 行为后 Runtime 关闭超时；同一当前 SDK/EXE/Web、每架构仅换内核的对照仍失败，x86 基线未运行。四组完整通过仍缺，原监督和授权守卫保留 |
| [96：关闭与发布依赖集成](https://github.com/chenziyang110/soui-web-sdk/issues/96) | 关闭/依赖代码及根验收版本漂移已修复；当前根报告正确绑定 0.6.0、unverified/collectionExecuted=false | 当前完整源码归档独立解压重建/搬移、完整汇总及其余生产/授权门，不重做已修复的状态漂移 |

## 证据复用与关闭规则

[当前支持矩阵](../acceptance/support-matrix.md)、[能力追踪](../acceptance/capabilities.md)和[三项结论](../acceptance/interim-decision.md)已明确区分 0.6.0 限定结果与历史 preview 表。历史结果继续绑定原源码、SDK、内核、环境和输入；当前输入改变时只补受影响项。

本轮对现有代码的独立核查没有找到支持再次改写上述运行时模块的具体反例，因此不重复已有修复。MessagePort 的旧注册失败、菜单/驻留失败、Win7 V7/V9/V10 及工具监督失败均保留；不得通过放宽 validator、抢占前台、延长总期限或强杀后追认成功来关闭 Issue。

## 本轮发现与修复

MessagePort 历史安装消费者的实际运行完成了挂载、三次重载和窗口关闭，40 行 sidecar 中的 4 文档、32 项浏览器行为经原读取器逐项校验通过。但首次 Runtime 关闭返回 `E_RUNTIME_SHUTDOWN_PENDING` 后，旧夹具立即失败，原生与监督器均退出 1；进程组自行清空、没有强杀。整次运行仍为失败，其他三臂未运行，不据此宣称四臂或当前 SDK 通过。

当前 `tests/message-port-app` 原有同样的单次关闭调用。本轮将其两个关闭步骤接到普通验收夹具已有的 `shutdown-poll.h`：只等待明确的 pending 状态，使用原 `until` 的固定期限、消息服务和等待；其他错误继续失败。随后当前 SDK 的真实运行还暴露了单次 reload 将 `E_RELOAD_PENDING` 直接记为失败的问题；现只继续同一 pending reload，首次成功后仅等 ready，操作和 ready 共用原 20 秒期限。原 9 个生命周期步骤、浏览器断言和资源关闭条件保持。

修复后的当前 0.6.0 安装消费者已完成两架构编译及 [VM 短测原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/vm/message-port-a2/run/raw/Summary.actual.json)：x64 11,002 毫秒、x86 10,944 毫秒，每架构完成九步、三次 reload、四文档、32 项浏览器行为和 40 行 sidecar，原生与监督器退出 0、Job 清空、没有强杀。首次 reload 失败原件独立保留，两组候选结果继续复用。

随后旧 0.5.1 SDK 四组对照的首组 x64 基线实际完成挂载、三次 reload、窗口关闭及四文档 32 项行为，但 Runtime 关闭在原 20 秒内未完成，原生自然退出 1、Job 清空，另外三组未启动。[失败原件及独立分析](../../verification/production-validation/open-issues-closeout-2026-10-29/paired-message-port/analysis/mp77-run-pa2/Analysis.actual.json)保留；准备阶段的两次失败与验证后续记录也单列，未将组合核验写成原尝试通过。

为排除 SDK 和消费者差异，只新增当前 SDK 的基线对照：两架构各 78 项运行文件中，77 项与已通过的候选逐字节一致，仅替换既定 `node.dll`。当前 x64 基线同样完成四文档、32 项行为后关闭超时，进程在 32,613 毫秒自然退出 1、Job 清空、没有强杀；[实际失败原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-sdk-pair/vm/run/raw/Summary.actual.json)没有被提升为通过。x86 基线未运行，两架构候选未重跑。此对照说明本次失败随内核差异出现；日志未采集准备状态、raw-active 计数或 Worker 栈，单凭日志不能确定具体 prepare、cursor 或 finalize 子阶段。#77 保持开放，不放宽关闭 fence 或延长期限来补齐四组成功。

随后[只读追查冻结源码](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/message-port/issue77-source-investigation/Analysis.actual.json)，确认旧 Worker 的已完成 Task 可持有 raw lease，直到延迟 timer 真正析构；空任务堆跳过回收并睡眠，与关闭准备形成可达的等待循环。当前内核已有的补丁 `0018` 在回合结束及仅需回收的唤醒后进行有界回收，并为剩余项唤醒，保留真实析构、资源 fence 和原生 join。源码机制与仅替换内核的结果支持这一解释，但没有捕获失败进程当时的计数或栈，不把源码推断写成现场子阶段证据。本轮未重编旧完整内核，也未重跑已通过的当前候选。

当前 0.6.0 公共托盘消费者的真实鼠标菜单/Esc 与快捷键/菜单选择测试仍返回 `E_TRAY_FOCUS`。安装、快捷键冲突探测和实际菜单输入已有记录，进程失败并确认清理。该失败保留，尚无证据将其归因于操作系统不支持；焦点与前台守卫仍保持。

随后在同一任务 VM 执行了一次不加载 SDK 的 x64 Win32 对照。真实右键菜单和 Esc 后，实际前台窗口已匹配 owner，单次 `NIM_SETFOCUS` 仍返回 FALSE；清理成功，进程和监督器均退出 1、Job 清空，没有强杀。[原件和独立分析](../../verification/production-validation/open-issues-closeout-2026-10-29/focus-control/analysis/a2/Analysis.actual.json)将结论限定为本 VM 的相同原生调用也会失败，不能证明 SDK 已修好或该 API 全局不受支持。立即读取的错误值只作诊断，不推导原因。更早的监督器缺少 CRT 启动失败保留为失败；没有 Job 回执，不宣称自然关闭。

针对回调尚未退栈的时序假设，只新增一次延迟原生对照：先投递 `WM_NULL`，原回调返回后再分派唯一完成消息，然后调用一次 `NIM_SETFOCUS`。原始记录确认两次消息出队有序、回调深度为 0、`InSendMessageEx=0` 且前台 owner 匹配，API 仍返回 FALSE。[有效对照及原件审查](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/focus/deferred-b1/analysis/Analysis.actual.json)仍为失败，清理成功并自然退出 1；该时序假设未解决问题，不能据此宣布 SDK 修复或系统/API 全局不支持。没有增加重试或放宽前台守卫。

为补齐独立的恢复缺项，新建 [三文件公共消费者夹具](../../tests/tray-recovery-consumer/README.md)，通过 `SouiSdk 0.6.0 EXACT` / `Soui::Sdk` 消费已有安装 SDK。在本任务 Windows Server 2022 VM 中，x64、x86 各由操作员使用 Task Manager 真实重启 Explorer 两次，中间只点击一次显式移除，最后点击结束。两次旧 Explorer 保留句柄均确认退出，新 PID/出生时间身份不同；第一次恢复成功，移除后第二次未复活，固定 500 毫秒观察后完成清理。每架构 37 条原生事件，[x64](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/recovery/vm/x64/raw/Summary.actual.json)/[x86](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/recovery/vm/x86/raw/Summary.actual.json) 原生分别在 52,303/74,160 毫秒退出 0，外部监督分别在 52,367/74,228 毫秒自然退出 0、Job 清空，无强杀或兜底。一次 90 秒内部期限及 100 秒外部监督、2 秒清理窗口未刷新。操作截图与日志组合支持本次真实恢复场景，`TaskbarCreated` 的 `sourceVerified=false` 保留；焦点、Win7 和完整 Issue 资格没有随之通过。[两架构汇总](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/recovery/analysis/x86/CombinedShortSummary.actual.json)保留此范围。SDK 代码未改，既有通过且输入未变的 MessagePort、Workbench、默认应用与其它短测未重跑。

修正检查工具后，当前 0.6.0 x64 Workbench 驻留短测实际完成初始三窗就绪、关闭中窗、其余两窗重载并重建中窗、全关后无窗驻留，以及最终三窗重建。整次仍退出 1：首次 `E_RELOAD_PENDING` 被通用结果记录器提前锁定为失败；末尾 Runtime 首次返回 pending 后，控制器也没有等待真实关闭。日志中的 `exit.cleanup-deadline` 是正常的退出计时开始事件，不是超时。

本轮在 Workbench 局部区分这两种明确的 pending 状态，真正的其他错误继续失败。Runtime 待完成期间保留控制器，复用原 10 秒总期限和按剩余时间限制的 50 毫秒重试定时器。新增 `reload-pending`、`runtime-pending` 两项直接策略回归已通过，分别 63/67 毫秒。两架构编译及 [修复后 VM 原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/vm/workbench-a1/run/raw/Summary.actual.json)已通过，x64 13,001 毫秒、x86 24,972 毫秒，覆盖三窗初始就绪、中窗关闭重建、存活窗口重载、无窗驻留和最终退出。两个进程均自然退出 0、Job 清空，没有强杀。x64 构建所属原尝试的 x86 CMake 警告失败仍保留，后续仅重建受影响的 x86 并显式组合两架构结果。既有输入未变的九项策略用例及默认应用短测不重复。

上述实际结果经独立原件复核，完整来源、原失败、输入与有界监督记录收于 [本轮闭集索引](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/Files.index.json)。虚拟机随后异常失联，原会话结论保持未知。两次恢复拒绝保留；宿主进程枚举恢复、核对调用进程后，按原磁盘和进程检查成功恢复同一 VM，没有运行宿主产品 GUI。后续对照失败如上，完整资格仍未通过。

普通 `tests/acceptance-app` 也有相同的单次 reload pending 误判，本轮一并修正。只重试精确 pending，首次成功后只等 ready，使用一次原 `until`；本次短测显式配置 20 秒，不改变默认参数合同。新的两架构安装消费者编译和 [有限生命周期短测](../../verification/production-validation/open-issues-closeout-2026-10-29/ordinary-reload/vm/run/raw/Summary.actual.json)已通过，x64 3,958 毫秒、x86 3,850 毫秒，各九步、三次 reload、七份采样及 27 条原生事件，自然退出 0、Job 清空，没有强杀。旧 Web 五份资产与对应源码和 SDK dist 的逐字节关系保留，未宣称当前 Vite/entry generator 重建或类型检查通过。完整 Collector、资源及生产资格仍缺，已通过的 MessagePort、Workbench 和默认应用未重跑。

许可材料组合的[首轮 v2](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/legal/published-060/reuse/attempt-980978e4-8027-4770-b664-94fcf114f036/Summary.actual.json) 因误把 source SDK 包根当作 `payload/` 根，207 次读取失败，实际材料计数为 1,031；这是核验配方路径错误，原尝试保留为 FAIL。v3 只改为正确的 `C:/s06sdk/payload` 及相应配方身份，组合核对已发布 0.6.0 的 1,238 份物理材料、六份应用清单及安装前缀的选定关系后通过[独立审查](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/legal/published-060/analysis/Analysis.actual.json)。既有归档/前缀闭集证明复用，没有重新解压或宣称当前分支、完整材料清单及法律资格通过；条款、再分发条件和维护者审批仍缺。

已定位[现有 Win7 x64 VMDK](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/win7/investigation/MediaRecovery.actual.md)，但本轮尚无该客体绑定当前载荷的实际 Win7 短测结果。找到磁盘不等于 Win7 已启动或兼容通过；历史 V11 缩放继续绑定原 x86 输入。

后续两次 CIM 预检失败仍保留；[r2 一次转换的独立复核](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/conversion/r2/independent-actual-review/Analysis.actual.json)仅确认转换通过：Node 与 PowerShell 各持有并释放 102 个源句柄，受检源身份未变、未写入源磁盘，受监督阶段 50,792 毫秒，宿主原生与监督器均退出 0、Job 清空。首个 x64 测试 VM 的[完整截图 05](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/boot-investigation/05-standalone-screenshot.actual.png)显示未找到操作系统；01–04 仍记为局部/未知，06 只对应界面 warm reset 后的黑屏及 WHPX 暂停。原 VMware 主盘使用 LSI SAS，而该 QEMU 配置使用 IDE，只是已知差异，根因未定。ACPI 请求后按准确 UUID 执行[受控 QMP quit](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/boot-investigation/PausedStop.actual.json)，随后[宿主退出 0](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/stopped-vm-metadata/stopped.json)，不代表来宾正常关机或产品通过；原盘、只读 base 和可写 overlay 均保留。

[第二份现有 x64 磁盘](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/second-standalone/observations/Report.md)仅核对 17 个主盘文件的元数据并只读采集 512 字节 MBR；没有新增转换或 VM，不能据此确认实际 OS、SP1、桌面或兼容资格。原 Win7 SP1 x86 环境仍缺，本轮 Win7 SDK 短测尚未运行。

[当前 x86 载荷图](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/win7/current-payload-map/Report.md)明确 671 文件、46,604,959 字节的完整映射，使用已通过 Server 短测的新修复 EXE `f3da178e…`，其余 670 项复用封存来源；尚未组装或搬入 Win7，已发布默认 EXE 和历史 V11 不替代该输入。Server 缓存、Node 24 和 PS5 控制脚本在新 Win7 环境中的可用性尚无证明。续期长测夹具缺口见 [#31 评估](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/issue31/assessment/Report.md)和 [D07 三步夹具交接](../testing/v0.6.0-long-run-handoff.md)：先补目录/媒体混合资源夹具、配套报告与分析器，再纳入统一真实长批次；现有 session 入口只覆盖任务/订阅。已通过且输入未变的短测继续复用。

其余环境与续期交接数据见[闭集索引](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff/Files.index.json)及[独立完整性审计](../../verification/production-validation/open-issues-closeout-2026-10-29/remaining-handoff-review.json)。

本次功能补充、保留失败与只读分析收于[功能收尾闭集](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout/Files.index.json)。归档过程不改变各项实际测试结论。[独立完整性审计](../../verification/production-validation/open-issues-closeout-2026-10-29/functional-closeout-review.json)核对了 275 个闭集文件和 269 份源副本；首次因 Node 符号链接路径被拒的归档副本另行保留，改用实际 Node 路径后只重做了数据复制。

八个 Issue 均保持开放，原有完整关闭条件仍有效。仍缺真实交互、目标系统或授权证据的项目继续单列。D07 真实租期、D13 千次、D14 八小时和其它长时资源/性能要求见 [统一长测交接](../testing/v0.6.0-long-run-handoff.md)，本轮不运行这些长测。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。普通 Release 分类、工程成员清单或必要短测均不改变这些结论。
