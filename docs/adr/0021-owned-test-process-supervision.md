# ADR-0021：测试进程通过持有的 Windows Job 监督

日期：2026-10-08。状态：接受，待对应原生测试验证。

## 背景

基准失败清理目前只停止直接子进程。Windows 父 PID 复用使数字 PID 树不能证明所有权；直接子进程退出也不能证明其浏览器或工具子进程已经退出。正式性能与长测需要排除自有残留。

## 决定

新增独立验证工具 `OwnedProcessRunner`。先创建无名称、不可继承的 Job，设置关闭时终止成员，创建暂停的直接子进程，成功加入 Job 后才恢复执行。不允许 breakaway，不通过服务名或父 PID 推断成员。

使用单调计时与原生进程句柄监督。记录 root PID、原生创建 FILETIME、Job 总成员/活动成员、原生退出码、超时与清理结果。根进程正常退出后仍有成员时仅给固定的结束宽限期；到期按失败处理并通过持有的 Job 清理。报告保持原失败，清理成功不会把超时或子进程残留变成成功。

正常结果须同时证明 root 退出成功和 Job 活动成员归零。报告新建写入，输出失败返回非零。验证覆盖正常退出、非零退出、三级子进程超时、root 提前退出、无关进程存活、参数与报告冲突。

成员镜像与身份快照是辅助诊断；进程自然退出时查询可能返回访问拒绝，必须保留查询错误，不能靠镜像路径决定清理所有权。活动归零证明来自持有 Job 的两次一致计数。`limitTerminatedProcesses` 只记录 Windows 因 Job 限制违规终止的成员数量；自然退出及主动 Job 清理不应从该字段推算。

需要外部步骤监督或故障诊断的入口可显式使用 `--stop-on-stdin`，仅接受监督器专用 stdin 管道中的 `STOP` 行。被测进程不继承该管道；请求、断连或非法控制均保持失败，由工具自己的 Job 清理并记录终态。调用者不通过数字 PID 杀被测进程，也不把终止监督工具当成已确认的整树清理。

被测进程通过 Windows 的显式 handle list 继承 stdout/stderr 的副本和只读 NUL 输入，保留原生命令诊断；Job、报告及控制管道不在列表中。属性列表 API 属于 Win7 已有的接口，实际 Win7 系统验证仍待完成。

## 备选方案

数字 PID 树可能误认复用进程；只杀 root 会遗留后代；按进程名清理会影响其他任务。现有只杀直接子进程的保守措施保留到调用点迁移并完成对应测试。

## 兼容与迁移

本工具是验证设施，不改变 SDK 运行期协议和打包内容。Win7 不支持嵌套 Job；加入失败时停止本次新建的暂停进程并报告失败，不启用脱离父 Job 规避检查。不同渲染器创建 Job 的行为需单独验证；工具不能证明通过 WMI、系统服务或外部 broker 创建的进程均在自身 Job 内，调用这些机制的负载需要额外所有权合同。

先以独立测试验证工具，后迁移基准及验收监督器，保持原有桌面互斥、载荷身份和完整资源采样。迁移未完成前不能宣称所有测试入口已有整树清理。

## 验证依据

加入失败分支采用独立的 `OwnedProcessAssignmentFixture` **测试专用编译目标**：只将该翻译单元的 `AssignProcessToJobObject` 替换为返回 `ERROR_ACCESS_DENIED`，直接编译生产 runner 源码，创建暂停进程、持有句柄、终止、等待、读取退出码与输出报告仍调用真实 Windows API。不新增生产参数开关，也不复制清理实现。夹具 stderr 明确标注 `TEST_ONLY_ASSIGNMENT_DENIAL`，原样本另存 `assignment-test-context.json` 记录注入范围和夹具二进制 SHA-256，明确 `win7NestedJobVerified: false` / `pcaVerified: false`。测试仅验证该失败分支未恢复新进程、实际退出确认和失败结论保持；不证明真实 Win7/PCA 或嵌套 Job 拒绝策略。

此前“enclosing Job 带 UI restriction”夹具在本机没有触发目标加入失败，原始失败输出保留。它不能作为拒绝策略已验证的依据；Win7/PCA 与不同渲染器目标验证继续未执行。成员镜像退出竞态的原始失败也保留，后续使用修正源码与新日志重新验证，不能覆盖原记录。

以 Windows API 实际结果为准。[微软 Job Objects 文档](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)说明 Job 默认的子进程关联、关闭清理与 Win7 嵌套限制；[AssignProcessToJobObject](https://learn.microsoft.com/en-us/windows/win32/api/jobapi2/nf-jobapi2-assignprocesstojobobject)规定加入失败条件。当前文档不代表目标系统验证已经完成。
