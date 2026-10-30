# ADR 0132：原 OwnedProcessRunner 的可选被动 Job cohort

状态：proposed；源码与测试待 Root 独审，未编译或执行。旧工具及旧证据保持原身份。

## 问题和边界

原 runner 在同一 held Job 内每次等待约 20ms 后读取 accounting，但只在 active/total 变化时记录 job-members。owned-member 没有批次完成边界，因此静默旧 raw 不能证明后续每周期 childResidual=0。D13 同 PID 的 warmup→calibration→measurement 需要可以绑定到实际时间窗口的外部观察，而非让 native consumer 声称自己查询 Job。

## 最小接口

只增加显式 `--cohort-interval-ms N`，十进制整数范围 20..1000，重复值和无效值在创建目标前拒绝。未传时，不创建 observer，不新增事件，也不改变原普通事件和关闭路径。首版只使用已有逐行 FlushFileBuffers 的 NDJSON report，不新增 latest 文件、owner、controller、端口或控制消息。

启用后在原循环中到期最多采样一次，不补跑错过的周期；observer 使用无堆分配的 optional 存储。原 suspended→assign→resume、同一 Job、20ms wait、timeout/drain 起点及 kill/自然关闭判定保持原样。采样正常的 Win32 查询失败只产生 unknown，不影响原关闭判定；observer 内部异常不产生成功样本，后续观察永久 unknown，失败尝试仍按间隔限频。原 report 写入失败仍沿原错误处理。

每条 `owned-cohort` 使用 schema `soui.owned-job-cohort/v1`，包括：

- sampleId：从 1 起的 canonical decimal string；到达饱和边界时永久 unknown、sampleId=null，不回绕或复用；observer 异常锁存后同样为 null。
- subjectPid、subjectCreationFileTime：原 start 的目标身份，不是 runner PID。
- queryBeginTickMs、queryEndTickMs：实际 GetTickCount64 的 canonical decimal string；另保留原 elapsed 的查询窗口，不能混用两种原点。
- accountingBefore/accountingAfter：原 Job 的 activeProcesses、totalProcesses、limitTerminatedProcesses；失败时计数字段为 null，保留 Win32 错误。
- assignedProcesses、returnedProcesses、members：单批固定最多 256 个 PID。每个实际打开的进程句柄以最少 QUERY_LIMITED_INFORMATION|SYNCHRONIZE 权限保留到本批检查结束，核 IsProcessInJob、creation FILETIME 和 WaitForSingleObject(handle,0)。PID/birth 或活性不明保留 unknown，不用零代替。
- complete/status/reason：两次 accounting 相等、完整返回数等于 assigned/active、无重复/非法 PID、全部 held identity/membership/liveness 有效才 complete；枚举溢出、查询失败、成员退出或前后变化都 unknown。
- outsideJobResidual、borrowedResourceResidual 固定 null；authority 固定 observation-only。

这是 query window 内有界一致的本 Job 样本，不是所有 OS 进程的原子快照。tick/elapsed 窗口截止于最终查询之后、JSON 序列化与 report flush 之前；consumer 必须按实际 tick 窗口另判 freshness。image 路径不属于此新增样本的完整性条件；原 owned-member 继续保留原 image 证据。conhost 及其它实际成员一律保留，不擅自豁免或推导 childResidual=0。

## 消费和安全边界

consumer/Root 可读取已完整换行的原 report 前缀，绑定精确字节/SHA、原 runner 新身份、subject PID/birth 和实际 phase receipt。文件不能授权输入、恢复、杀进程、延长预算或授予测量通过；unknown/stale 样本不能被置零。它不会向 consumer 传递 Job 或进程句柄，也不会扩大 outside-Job、WMI/服务代建进程、借用资源的覆盖。

启用后的查询和 report flush 会增加观测成本，不承诺 20ms 调度 SLA、严格最终返回上界或性能零扰动。新二进制必须另路径构建并记录身份，不覆盖旧 df0afe34… 工具；默认模式的源投影和原关闭测试需独立核对。

## 计划验证

先写不执行的显式新 runner 集成测试：默认无 cohort；opt-in 同 Job/同 birth 的有效窗口；真实子进程保留；非法/重复参数在目标创建前失败；opt-in timeout 仍按原 Job 清理。测试通过 `SOUI_OWNED_COHORT_RUNNER` 明确选择未来新工具，拒绝旧工具 hash。Win32 查询错误/枚举上限/ID 饱和的完整覆盖不能仅凭这些成功样本宣称已验证；SOURCE 独审仍需检查各 unknown 分支。Root 审核前不编译或执行。
