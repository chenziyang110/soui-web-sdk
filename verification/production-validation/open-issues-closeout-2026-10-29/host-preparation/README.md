# Issue 收尾：宿主准备、自然编译与基础设施记录

此目录只归档 2026-10-29 已实际产生的宿主证据，不重新运行构建、测试或 GUI。真实 VM 输入与结果由 Root 在其他目录另行记录，不从本目录的准备成功推断产品功能通过。

`tray/` 保存当前公共安装 SDK 的独立托盘消费者源码副本、输出工具、实际两架构配置/编译、Owned 原始流/上下文/stdout/stderr、VCTIP 两阶段、工具与完整安装文件输入前后 pin，以及编译器/链接器跟踪记录。各架构只编译一次，均在原 120000ms + 2000ms drain 内自然收尾；没有执行生成 EXE。EXE/OBJ 仍保留本地精确 pin，不在源码证据内重复嵌入。Node、ISO、完整 SDK 安装副本和 VM 磁盘不纳入。

`infrastructure/` 包含本地 QEMU 技能修改前的四份原文件、审查后 v4 的五份文件、各次必要短回归的实际记录、成功的明确离线恢复，以及之前安全可公开的超时拒绝。恢复保留原会话退出码未知、`priorOutcome=UNKNOWN` 与 `cleanShutdownVerified=false`，不是干净关机、GUI 或性能通过。基础设施源码作为历史证据保存，不是产品 SDK 或本目录可直接执行的日常操作指令。

首次调用参数错误的 stderr 曾包含当前 VM lease token。该原始文件只留在本地；`RedactedFailure.actual.json` 明确仅保存其 SHA、长度、退出码 2 和排除说明，不冒充原始日志，也不把失败改写为通过。不包含 UI 凭据、无人值守应答文件或 ISO。

`orphan-cleanup/` 是维护者持续授权下，对四个身份、父进程与稳定 CPU 已核对的遗留辅助进程所做的单独强制清理。它与随后真实编译自然完成的 Owned 结果分开，不作为任何正常关闭通过证据。

`IndependentReview.actual.json` 由 `/root/issues_runtime_audit` 本人记录 #39 工程归档与 v4 恢复代码的独立静态审查。未重跑已通过且输入未变的 verifier/短测。托盘准备由同一 reviewer 编写，明确不把自查称为独立审查。

`HostFiles.index.json` 对本目录全部原字节建立完整闭集，只排除自身。`collect-host.cjs` 是一次归档入口，已存在结果时拒绝覆盖；本目录不提供执行产品或重新取得测试通过的入口。原件绝对路径仅是当时的来源记录。`releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。
