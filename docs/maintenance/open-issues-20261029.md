# 未关闭 Issue 逐项收尾，2026-10-29

本轮从主线 `0cdc0a9` 核查八个开放 Issue 的实现、评论与已有证据。维护者要求只补必要短测，输入未变且已通过的结果不重复，长测后置。源码已实现、限定场景通过和完整资格通过分别记录；不修改已发布 v0.6.0 的资产或历史失败。

| Issue | 已完成或可复用范围 | 剩余必要工作 |
|---|---|---|
| [23：生产能力与采用价值](https://github.com/chenziyang110/soui-web-sdk/issues/23) | 当前两架构构建、安装消费者、默认应用八次运行及归档闭集见 [0.6.0 实际记录](../../verification/production-validation/0.6.0-release-2026-10-29/RELEASE-FACTS.json) | 汇总下列专项、完整独立交付及 Collector、目标系统/资源/性能与采用价值。千次和八小时按后置范围执行 |
| [27：公共托盘](https://github.com/chenziyang110/soui-web-sdk/issues/27) | 公共服务、独立安装消费者和原故障边界已实现；旧 fake-platform 与限定 Shell 结果保留 | 当前真实可见/点击、Explorer 重建、移除不再恢复、所有权与最终退出，Win7 单列 |
| [29：菜单、快捷键与驻留](https://github.com/chenziyang110/soui-web-sdk/issues/29) | 公共模块已实现；本轮修复后 x64/x86 有限三窗、重载、无窗驻留及最终退出短测通过，原失败保留 | 真实菜单/快捷键、Explorer 恢复、三窗业务隔离、模态关闭与 Win7 仍缺；原前台检查和期限不变 |
| [31：同文档续期](https://github.com/chenziyang110/soui-web-sdk/issues/31) | 原生续期、稳定资源归属、Web 单飞续期及独立容量通道已实现，原契约/安装消费者结果保留 | 浏览器双层满载、任务/订阅/目录/媒体保留及 Win7；真实超过 30 分钟和八小时后置，模拟时钟不替代 |
| [39：依赖许可与链接成员](https://github.com/chenziyang110/soui-web-sdk/issues/39) | 首方 MIT 决定及随包原许可已记录；本轮新增 [0.6.0 精确 DLL/MAP 全部来源与对象成员记录](../../verification/production-validation/current-link-members-0.6.0-2026-10-29/README.md)及 [24 个组件的工程对应清单](../../verification/production-validation/current-license-crosswalk-0.6.0-2026-10-29/README.md)，保留 zlib 版本冲突及 OTS/Skia 等未知身份 | 完整条款/嵌套与文件级审核、来源冲突补证、责任人与维护者审批；候选源文件和相同许可文本不等于完整传递依赖或法律批准 |
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

为排除 SDK 和消费者差异，只新增当前 SDK 的基线对照：两架构各 78 项运行文件中，77 项与已通过的候选逐字节一致，仅替换既定 `node.dll`。当前 x64 基线同样完成四文档、32 项行为后关闭超时，进程在 32,613 毫秒自然退出 1、Job 清空、没有强杀；[实际失败原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-sdk-pair/vm/run/raw/Summary.actual.json)没有被提升为通过。x86 基线未运行，两架构候选未重跑。此对照说明本次失败随内核差异出现；日志未记录具体 worker prepare、cursor 或 finalize 阶段，不能据此归因。#77 保持开放，不放宽关闭 fence 或延长期限来补齐四组成功。

当前 0.6.0 公共托盘消费者的真实鼠标菜单/Esc 与快捷键/菜单选择测试仍返回 `E_TRAY_FOCUS`。安装、快捷键冲突探测和实际菜单输入已有记录，进程失败并确认清理。该失败保留，尚无证据将其归因于操作系统不支持；焦点与前台守卫仍保持。

随后在同一任务 VM 执行了一次不加载 SDK 的 x64 Win32 对照。真实右键菜单和 Esc 后，实际前台窗口已匹配 owner，单次 `NIM_SETFOCUS` 仍返回 FALSE；清理成功，进程和监督器均退出 1、Job 清空，没有强杀。[原件和独立分析](../../verification/production-validation/open-issues-closeout-2026-10-29/focus-control/analysis/a2/Analysis.actual.json)将结论限定为本 VM 的相同原生调用也会失败，不能证明 SDK 已修好或该 API 全局不受支持。立即读取的错误值只作诊断，不推导原因。更早的监督器缺少 CRT 启动失败保留为失败；没有 Job 回执，不宣称自然关闭。

修正检查工具后，当前 0.6.0 x64 Workbench 驻留短测实际完成初始三窗就绪、关闭中窗、其余两窗重载并重建中窗、全关后无窗驻留，以及最终三窗重建。整次仍退出 1：首次 `E_RELOAD_PENDING` 被通用结果记录器提前锁定为失败；末尾 Runtime 首次返回 pending 后，控制器也没有等待真实关闭。日志中的 `exit.cleanup-deadline` 是正常的退出计时开始事件，不是超时。

本轮在 Workbench 局部区分这两种明确的 pending 状态，真正的其他错误继续失败。Runtime 待完成期间保留控制器，复用原 10 秒总期限和按剩余时间限制的 50 毫秒重试定时器。新增 `reload-pending`、`runtime-pending` 两项直接策略回归已通过，分别 63/67 毫秒。两架构编译及 [修复后 VM 原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/vm/workbench-a1/run/raw/Summary.actual.json)已通过，x64 13,001 毫秒、x86 24,972 毫秒，覆盖三窗初始就绪、中窗关闭重建、存活窗口重载、无窗驻留和最终退出。两个进程均自然退出 0、Job 清空，没有强杀。x64 构建所属原尝试的 x86 CMake 警告失败仍保留，后续仅重建受影响的 x86 并显式组合两架构结果。既有输入未变的九项策略用例及默认应用短测不重复。

上述实际结果经独立原件复核，完整来源、原失败、输入与有界监督记录收于 [本轮闭集索引](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/Files.index.json)。虚拟机随后异常失联，原会话结论保持未知。两次恢复拒绝保留；宿主进程枚举恢复、核对调用进程后，按原磁盘和进程检查成功恢复同一 VM，没有运行宿主产品 GUI。后续对照失败如上，完整资格仍未通过。

普通 `tests/acceptance-app` 也有相同的单次 reload pending 误判，本轮一并修正。只重试精确 pending，首次成功后只等 ready，使用一次原 `until`；本次短测显式配置 20 秒，不改变默认参数合同。新的两架构安装消费者编译和 [有限生命周期短测](../../verification/production-validation/open-issues-closeout-2026-10-29/ordinary-reload/vm/run/raw/Summary.actual.json)已通过，x64 3,958 毫秒、x86 3,850 毫秒，各九步、三次 reload、七份采样及 27 条原生事件，自然退出 0、Job 清空，没有强杀。旧 Web 五份资产与对应源码和 SDK dist 的逐字节关系保留，未宣称当前 Vite/entry generator 重建或类型检查通过。完整 Collector、资源及生产资格仍缺，已通过的 MessagePort、Workbench 和默认应用未重跑。

Issue 原有完整关闭条件仍有效。仍缺真实交互、目标系统或授权证据的 Issue 保持开放。D07 真实租期、D13 千次、D14 八小时和其它长时资源/性能要求见 [统一长测交接](../testing/v0.6.0-long-run-handoff.md)，本轮不运行这些长测。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。普通 Release 分类、工程成员清单或必要短测均不改变这些结论。
