# MessagePort 主机准备快照

本目录仅归档 Issue #77 的主机准备：真实历史 `0.5.1-preview.1` SDK/消费者/Web 的复用审查、两个公开 candidate stage、传输载荷及 ISO 工具执行。**不包含浏览器运行结论**；后续客体 preflight 与四臂原始结果由 Root 单独归档，不能由准备成功推导。

- `Sources.index.json` 逐文件绑定原路径、复制前后 SHA-256 与归档字节；原文件未编辑，嵌入的原生产者路径、时间、失败与 pending 标志原样保留。
- `Files.index.json` 覆盖完整目录文件集合，只排除自身避免自引用；`Integrity.actual.json` 记录实际复制、有限 token 模式检查及收集器命令。收集过程没有重跑 SDK、测试、stage、ISO 或 VM。
- `host/` 保留 projection、draft/approved 配置和原审查记录。原 projection 明确是历史真实 PrepareOnly 的当前审查投影，不能冒称新编译。guest 执行脚本的存在或 `executionEnabled=true` 配置均不是运行证据。
- `stages/` 保存 x64/x86 两次公开 stage 的完整 stdout/stderr、Owned/context、实际结果。它们复用原 EXE/Web/SDK，只实际 install/stage；各对 runtime 仅 `node.dll` 不同，不认证整个 0.6.0 SDK。
- `carrier/` 保留完整 carrier manifest、读闭包、复制清单、源计划与目录读回；ISO、完整源码/安装树、EXE/DLL/Node 和 scratch 不重复归档，其精确 pin 在对应原件与 `Snapshot.actual.json` 中。

ISO 原失败完整保留：r1 在 262 字符目录打开时返回 Win32 error 3；r2 将系统 COM DLL 误纳入单硬链接 lease 后拒绝其 Windows 系统硬链接。最终 r3 只为 native open/AddTree 使用扩展路径，并恢复原可信系统 COM 的有界前后 SHA/CLSID 边界；源码/载荷的 reparse、单硬链接、身份、共享、散列及 EOF fence 不变。

原 source plan 声明 `fileSystems=7`，实际媒体明确选择严格 UDF-only `4` / revision `0x201`：该树深度 21，而 ISO9660 严格限制 8 层。**最终有效版本**是 `host/Write-IssuesIso.r3.ps1`、`host/Run-IsoWriter.r3.cjs` 及 `carrier/iso-writer-r3-3be2d976-6f91-4a49-9123-a0875404ebb2/` 的实际 inputs/result；源计划字节未改。`IsoR3.boundary.json` 与 `*.before-udf.*` 是执行前草稿，`LongPathIso.derivative*.json` 亦不是最终 writer pin，不得替代 r3 实际输入。

r3 在原 `120000ms` / `2000ms` 内自然 root0/supervisor0/Job0，51,633ms；目录读回仅证明 3032 文件、1185 目录的名称和大小，主机没有重复全量解压。该准备时点仍需客体完整 raw hash；后续实际执行属于另行记录。

`*.review-failed*`、`Diagnose-IsoLease.v1.ps1` 及旧生成器保留审查/工具草稿身份，不是重新执行成功记录。Root 审查记录按各自原 scope 引用，未编造新审查结论。`currentSdk060Qualified=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`、`releaseReady=false`。
