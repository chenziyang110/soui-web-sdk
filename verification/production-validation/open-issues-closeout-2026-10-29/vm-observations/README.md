# 本轮 VM 实际观察与失败原件

此快照只复制已经存在的离线解码、VM 截图及数据介质工具原件，不重跑应用、验收器、编译或 VM。`SOURCE-MANIFEST.json` 逐文件记录源路径、原 mtime、复制前/后与副本 SHA；`Files.index.json` 关闭整个归档集合，仅排除自身。原生产者路径、时间、失败、unknown 和 false 字段均不改写。

| 场景 | 已实际观察的范围 | 结论与未运行项 |
|---|---|---|
| MessagePort preflight2 | 客体完整核对 2991 文件、816132513 字节，历史四个套件身份有效 | 只通过载荷预检，没有运行浏览器 |
| MessagePort run1 | 仅尝试 x64 baseline；旧 runner 加载返回 `0xC0000135` | 没有 subject 身份或完整 Job 闭合证明；其余三臂未跑 |
| MessagePort run2 | 原历史 x64 baseline 四文档留下 40 行 sidecar，其中 32 项有限行为为 pass | Runtime shutdown 首次 `E_RUNTIME_SHUTDOWN_PENDING` 被旧消费者当作终态；原生 root/supervisor 均 1，Job 0、没有强杀。其余三臂未跑；不能称四臂通过 |
| 托盘 tray2 | 当前 0.6.0 x64 托盘安装、真实 callback/menu 返回及明确清理有记录 | `E_TRAY_FOCUS` 后 root/supervisor 1，Job 0、没有强杀；本批 x86 未跑。清理成功不能抹掉功能失败 |
| 驻留 resident1 | 原夹具因 runtime 根新增 `webview-smoke.log` 触发严格闭集拒绝 | 预检失败，产品未启动，未运行另一架构 |
| 驻留 resident2 | 当前 0.6.0 x64 三窗 ready、中窗关闭/重建、全部关闭后无窗驻留及最终三窗重建均到达 | 首次 reload pending 已污染永久退出码，Runtime pending 又被 one-shot 当失败；root/supervisor 1、Job 0，无强杀；x86 未跑 |
| tray3 x86 | 真实 Ctrl+Alt+F9 对应一次原生 shortcut activation，中文 checked/disabled 子菜单可见，原生 selection 1 返回 | 执行提示更新之前发生 `E_TRAY_FOCUS`，root/supervisor 1、Job 0、cleanupConfirmed=true，无强杀；不声称提示已更新或整项通过 |

`message-port/` 是实际旧 `0.5.1-preview.1` 公共消费者/EXE/Web，源码身份 `652a66973f09c5a90a5e06465944dd7ea8d3a3d9463411db5b07d045cb843973`。其配对候选只换精确当前 0.6.0 DLL，然而候选臂均未运行；此处有限 baseline 行为不认证当前 0.6.0 SDK。Worker 自关闭/terminate 的 Web 观察明确仍是 `native-join-unobserved`，也没有 debugger-wait 证明。

`desktop/` 绑定当前 0.6.0 tray/Workbench 载荷。resident2 的 PID 1132、birth `134377368324407305`：seq36/37 的 `E_RELOAD_PENDING` 随后已真实成功，但旧 `host_result` 先锁定 fail(1)；seq135 在 10563ms 完成最终三窗重建，seq136 `exit.cleanup-deadline` 是正常 **begin** 事件，非超时。真正超时事件名是 `exit.cleanup-timeout`。seq371 的 Runtime pending 后旧控制器先返回；125ms 后 main fallback 成功不能把已失败结果翻为通过。

`control-r2/` 保存原 r2 控制脚本、manifest、准备及原审查消息回执，只允许根 `webview-smoke.log` 可变，其他 36 payload 行与两个 671-file resident 清单保持；无依据的 source-navigation 忽略已取消。原控制器 ready/smoke/Owned/drain 与清理总期限不变。复制和 SHA 拒绝的第一次尝试未启动产品，原件不覆盖。

`media/e-image-cap-failure/` 保留 caller 32MiB 与原 helper 固定 1.4e9 字节边界不匹配的 `E_IMAGE_CAP` 数据失败；`media/success/` 保留恢复原 cap 后的真实自然完成，helper 和 120000/2000ms 预算没有修改。小 ISO 仅记录 pin：1507328 字节，SHA-256 `09c060b2f9917c39a709fe64f6c8f71911a6e6e383b1f653bf7fcc13a8575d03`；不复制 ISO/scratch，不从工具成功推定产品或全部客体内容通过。

`launchers/{resident2,tray3}/` 保存随后取得的两个独立导出：各原 catalog + 5 文件。原 PowerShell 收据均为 nativeExitCode=1/exitCode=1、environmentRestored=true、leaseReleased=true、restoreErrors=[]，精确桌面 mutex 的 PID/birth 收据保留。它们证明已记录的 launcher 环境恢复与退出状态，不能把产品失败改为通过。

`gui/` 六张均为本任务 VM 截图。图片辅助显示观察，不代替原生输入/进程终态。`RootObservations.received.json` 保留当时两次 CUA 请求未派发、暂停输入及 tray3 尚无终态的历史观察；没有本地原始工具文件或精确发生时间，不编造回执。随后状态以 `RootObservations.final.received.json` 与当前 `ClaimSummary.json` 为准：CUA 已恢复、Shift 唤醒屏保；首次 tray3 导出命令丢首字母而未执行，纠正后才取得完整 tag 和原生失败收据。

foreign compiler 阻塞修复后编译，当前 Workbench 与 MessagePort 源码修复已获静态审查零阻断，但尚未编译/VM 短测。其来源是 `current-source-preparation/RootSourceReviews.received.json`：Root 对本聊天实际收到的两次独立审查消息所作摘要，绑定七个源码文件；它不是 raw message、工具输出或测试执行。

`current-source-preparation/RootCrosswalkReview.actual.json` 仅记录 Issue #39 许可工程 crosswalk 的独立 XML/MASM 项目节点与索引核对，不是上述应用/poll 源码审查，也不构成法律审批；原件及其原有索引均未修改。同目录 `PackageAudit.observed.json` 是 Root 对两个实际 npm audit 均退出 0、报告零漏洞的观察摘要，并绑定 SDK Web/Workbench 锁文件；它明确不是 raw stdout，没有安装依赖，不是 Runtime 或生产验收。原失败仍保留。

`Collection.actual.json` 和 `TokenAudit.actual.json` 记录首批 136 原件；`Collection.append.actual.json` 与追加 token audit 记录随后 38 原件，以及最后的 Root package audit 和源码审查摘要收据。当前 SOURCE-MANIFEST/Files.index 合并全部 176 份源原件，只作字节复制及元数据校验，未重跑任何旧验收器。

`TokenAudit.actual.json` 只记录实际 VM lease token 的内存扫描结果，不保存值或 digest，不复制私有 QEMU catalog/lease 文件或含 token 的 recovery stderr。客体 `Lease.actual.json` 只是桌面 mutex 的 PID/birth 收据；它与私有 QEMU lease 不同。只保留指定解码 frame/envelope，不保存整份 live serial。

长测继续后置，已通过且输入未变的项不重复；当前必要短项仍按各 Issue 的原范围完成。`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`，本快照不生成更宽资格。
