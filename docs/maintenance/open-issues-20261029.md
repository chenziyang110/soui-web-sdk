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
| [77：MessagePort fixture](https://github.com/chenziyang110/soui-web-sdk/issues/77) | `caseId.maxLength=24`、`status.maxLength=11` 已合入；持久路径双架构实际准备和选择回归已有证据 | 持久 fixture 双架构 baseline/candidate 四臂真实浏览器行为与自然 Job 闭合。每臂 4 文档、32 行为、40 sidecar 行，原监督和授权守卫保留 |
| [96：关闭与发布依赖集成](https://github.com/chenziyang110/soui-web-sdk/issues/96) | 关闭/依赖代码及根验收版本漂移已修复；当前根报告正确绑定 0.6.0、unverified/collectionExecuted=false | 当前完整源码归档独立解压重建/搬移、完整汇总及其余生产/授权门，不重做已修复的状态漂移 |

## 证据复用与关闭规则

[当前支持矩阵](../acceptance/support-matrix.md)、[能力追踪](../acceptance/capabilities.md)和[三项结论](../acceptance/interim-decision.md)已明确区分 0.6.0 限定结果与历史 preview 表。历史结果继续绑定原源码、SDK、内核、环境和输入；当前输入改变时只补受影响项。

本轮对现有代码的独立核查没有找到支持再次改写上述运行时模块的具体反例，因此不重复已有修复。MessagePort 的旧注册失败、菜单/驻留失败、Win7 V7/V9/V10 及工具监督失败均保留；不得通过放宽 validator、抢占前台、延长总期限或强杀后追认成功来关闭 Issue。

## 本轮发现与修复

MessagePort 历史安装消费者的实际运行完成了挂载、三次重载和窗口关闭，40 行 sidecar 中的 4 文档、32 项浏览器行为经原读取器逐项校验通过。但首次 Runtime 关闭返回 `E_RUNTIME_SHUTDOWN_PENDING` 后，旧夹具立即失败，原生与监督器均退出 1；进程组自行清空、没有强杀。整次运行仍为失败，其他三臂未运行，不据此宣称四臂或当前 SDK 通过。

当前 `tests/message-port-app` 原有同样的单次关闭调用。本轮将其两个关闭步骤接到普通验收夹具已有的 `shutdown-poll.h`：只等待明确的 pending 状态，使用原 `until` 的固定期限、消息服务和等待；其他错误继续失败。随后当前 SDK 的真实运行还暴露了单次 reload 将 `E_RELOAD_PENDING` 直接记为失败的问题；现只继续同一 pending reload，首次成功后仅等 ready，操作和 ready 共用原 20 秒期限。原 9 个生命周期步骤、浏览器断言和资源关闭条件保持。

修复后的当前 0.6.0 安装消费者已完成两架构编译及 [VM 短测原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/vm/message-port-a2/run/raw/Summary.actual.json)：x64 11,002 毫秒、x86 10,944 毫秒，每架构完成九步、三次 reload、四文档、32 项浏览器行为和 40 行 sidecar，原生与监督器退出 0、Job 清空、没有强杀。首次 reload 失败原件独立保留。这两组当前 SDK 结果不替代 #77 所需旧 SDK 的 baseline/candidate 四组对照；后者的新消费者编译已通过，真实浏览器执行仍待完成。

当前 0.6.0 公共托盘消费者的真实鼠标菜单/Esc 与快捷键/菜单选择测试仍返回 `E_TRAY_FOCUS`。安装、快捷键冲突探测和实际菜单输入已有记录，进程失败并确认清理。该失败保留，尚无证据将其归因于操作系统不支持；焦点与前台守卫仍保持。一次原生 Win32 对照已准备并审查，尚未运行。

修正检查工具后，当前 0.6.0 x64 Workbench 驻留短测实际完成初始三窗就绪、关闭中窗、其余两窗重载并重建中窗、全关后无窗驻留，以及最终三窗重建。整次仍退出 1：首次 `E_RELOAD_PENDING` 被通用结果记录器提前锁定为失败；末尾 Runtime 首次返回 pending 后，控制器也没有等待真实关闭。日志中的 `exit.cleanup-deadline` 是正常的退出计时开始事件，不是超时。

本轮在 Workbench 局部区分这两种明确的 pending 状态，真正的其他错误继续失败。Runtime 待完成期间保留控制器，复用原 10 秒总期限和按剩余时间限制的 50 毫秒重试定时器。新增 `reload-pending`、`runtime-pending` 两项直接策略回归已通过，分别 63/67 毫秒。两架构编译及 [修复后 VM 原件](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/vm/workbench-a1/run/raw/Summary.actual.json)已通过，x64 13,001 毫秒、x86 24,972 毫秒，覆盖三窗初始就绪、中窗关闭重建、存活窗口重载、无窗驻留和最终退出。两个进程均自然退出 0、Job 清空，没有强杀。x64 构建所属原尝试的 x86 CMake 警告失败仍保留，后续仅重建受影响的 x86 并显式组合两架构结果。既有输入未变的九项策略用例及默认应用短测不重复。

上述实际结果经独立原件复核，完整来源、原失败、输入与有界监督记录收于 [本轮闭集索引](../../verification/production-validation/open-issues-closeout-2026-10-29/current-execution/Files.index.json)。虚拟机随后异常失联，原进程消失但未取得正常关机回执；原会话结论保持未知。恢复被宿主进程枚举“调用已取消”拦住，未修改磁盘或运行宿主产品 GUI。后续四组对照、托盘诊断与完整资格保持待验证。

Issue 原有完整关闭条件仍有效。仍缺真实交互、目标系统或授权证据的 Issue 保持开放。D07 真实租期、D13 千次、D14 八小时和其它长时资源/性能要求见 [统一长测交接](../testing/v0.6.0-long-run-handoff.md)，本轮不运行这些长测。

`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。普通 Release 分类、工程成员清单或必要短测均不改变这些结论。
