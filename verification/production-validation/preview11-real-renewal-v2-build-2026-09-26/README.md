# preview.11 续期夹具 v2：编译与无 GUI 验证证据

本目录归档外部 v2 的真实 x86/x64 构建：每架构 **13 个步骤均原始退出 0**，外层 Build 也由 Capture-Command 保存完整 stdout/stderr 和数值退出码 0。两架构 native self-check 均确认 records=8、fixtureRevision=2、phaseCases=45。分析器 **106 项合成用例通过**，只证明解析器测试行为。

**v2 GUI 尚未执行**；没有短 smoke、35 分钟、8 小时、Win7、资源、性能或生产通过结论。原 v1 实际 x86 smoke 退出 1、ready/任务为 0 的失败仍原样保留，v2 编译不抹去它。

## 失败与修正原样保留

第一轮 prepare-verify 的 capture 包装器对只读文件描述符 fsync，发生 EPERM，未写出 child result；保留该目录的原始 stdout/stderr、wrapper-failure-observation 和旧 Capture 源码。**不能从 stdout 倒推出第一轮 child 退出码 0。** 随后将描述符改为 r+ 并保留错误记录逻辑，独立 prepare-verify-attempt2 才有实际数值退出码 0。

review-history 保存 runner/Prepare 绑定修订前源码；最终两架构 build/protocol/driver manifests 绑定实际使用的版本。历史 preparation-status 和 frozen PROTOCOL 中的“当时未编译”描述是原准备时点记录；当前执行状态以 raw/execution-status.json 和相应 raw receipt 为准，不改历史冻结文件。

## 审计与限制

先核对两架构 package.json/package-lock.json 原字节一致，并完整检查两个安装工具链，随后仅在 x86 相同锁输入下执行一次 npm audit --json。实际退出码 1，漏洞总数 不可用；两架构工具链前后未变 true。完整参数、时间、原字节 stdout/stderr 与退出码在 raw/audit-build-20260926。审计结果不代表生产或许可审核。

实际链接仍受到宿主全局 vcpkg 集成影响，F:\GitHub\vcpkg\installed\<arch>-windows\lib 路径和附加库未冻结。两架构 link.command.1.tlog、工程与 CMake 配置已归档；**不能声称封闭可复现或干净机通过**。

## 文件身份

- v2 freeze SHA-256：`80d19d4e2008a505bbb8a9293849f3756d4bf07ad7838ad38aa5bb054ac0ee3a`。
- SDK 源码输入 SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`，版本 0.3.0-preview.11，SDK 内容未变。
- 父 v1 freeze SHA-256：`7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d`；完整父冻结位于先前 preview11-real-renewal-build-2026-09-26 编译归档，当前保留身份与必要父记录。
- raw/ 包含最终夹具源码（含两个 header）、脚本/协议、冻结与准备清单、历史修订、每架构所有步骤（包括 finalize）和外层捕获原日志、CMake/link 命令、106 项合成报告及独立评审。
- raw-manifest.json 对每个复制文件记录长度和 SHA-256；Archive-V2-Build.cjs 拒绝覆盖、逐项复制后再次核对源与目标原字节。未复制二进制、node_modules、完整 SDK 输入或模拟长测大流。

后续必须通过桌面互斥与外部 QEMU 门禁，串行执行 v2 真实 smoke，再视结果安排实际 35 分钟/8 小时验证。所有 realRenewalVerified/resourceGate/benchmarkValid/releaseReady 保持 false。

本轮依赖审计未通过：npm 服务在 TLS 连接建立前断开，原始退出码为 1，漏洞统计不可用。没有重试，也没有把网络失败计为 0 漏洞或审计通过。

生产 Run.ps1 内嵌 checker 的独立检查为 8/8：两个正例只读检查实际 v2 冻结输入，六个负例使用重算散列的合成篡改记录并被预期门禁拒绝。raw/runner-binding-checks 保留精确抽取脚本、身份、各例原退出码/日志；未执行原生应用或 GUI，不能作为 app smoke 或续期通过。
