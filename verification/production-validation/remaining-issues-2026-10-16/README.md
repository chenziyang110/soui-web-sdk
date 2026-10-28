# 首方 MIT 与剩余 Issue 验收准备

2026-10-16，开发版本 0.5.1-preview.1，尚未发布。维护者明确选择首方 MIT，并恢复按各 Issue 的关闭条件执行长测。此记录只证明许可证交付、升级回退、相关回归和验收程序准备；真实桌面行为、Win7、8 小时 session、8 小时 mixed、完整第三方授权审阅均未完成。八个 Issue #23/#27/#29/#31/#39/#42/#77/#96 继续开放，`releaseReady=false`。

## 实际结果

- 46 项 SDK 交付、身份、版本、原生载荷与 GUI 包回归全部通过；命令为 `node --test tests/sdk/gui-bindings-delivery.test.cjs tests/sdk/legal-notice-materials.test.cjs tests/sdk/delivery-identity.test.cjs tests/sdk/sdk-version.test.cjs tests/sdk/sdk-native-payload.test.cjs`。
- 最终 GUI 构建后的 React/Vue/包消费 28 项、React 18 的 11 项及两套类型检查通过。四份 GUI npm 审计和额外 Web SDK、验收工具审计均为零漏洞；这不是桌面兼容验收。
- 使用真实冻结 0.5.0 SDK 包执行 `0.5.0 → 0.5.1-preview.1 → 0.5.0 → 0.5.1-preview.1`。新包携带与根目录相同的 LICENSE，回退恢复原包文件集合，业务文件不变，原 0.5.0 包清单散列不变。新包为默认源 SDK；旧包另含可选 GUI 输出，文件数量差异不代表丢失业务文件。
- x64/x86 Release 各完成独立 MessagePort 和 ordinary 消费者准备，共四组。每组经标准进程监督正常退出，整个 Job 自然闭合，没有强杀或 fallback；安装的许可证均与根目录原字节一致。归档前 `tests/acceptance/runner.cjs` 的 `verify` 已重新核对四组实际源码、SDK、Web、安装与载荷身份。
- 当前内核二进制未变更；链接成员证据另见 [当前链接复核](../current-link-members-2026-10-16/README.md)。

## 保留的失败与边界

首次版本迁移后，独立消费者仍要求 0.5.0 EXACT，配置失败；`raw/failed-version-pin/` 保留原捕获和配置日志，修正版本要求后生成全新套件。首次 GUI 构建因 peer 范围守卫仍旧而失败，保留 `raw/commands/gui-build.log`。随后真实回退又暴露旧包必须保留旧 peer 契约，已按版本选择校验规则，最终 46 项回归和真实回退均通过。中间构建和失败的原目录仍留在本地，没有覆盖成成功结果。

准备阶段没有执行桌面场景。另一项目的 QEMU 占用期间遵守桌面隔离约束；维护者已安排独立 Luna 会话接管后续验证，需等空闲并再次验证套件身份。运行中、等待中或准备成功均不能作为关闭 Issue 的依据。

## 复核

`INDEX.json` 保存每份原件的来源、字节数和 SHA-256，`raw/` 保留原字节。`SUMMARY.json` 列出最终四组套件和构建身份。开发机路径仅是证据来源，不是产品配置。仓库根目录执行：

```powershell
node verification/production-validation/remaining-issues-2026-10-16/verify.cjs
```

该命令检查原件散列、正常进程闭合、构建关联、许可证、升级回退和 GUI 日志；它不重新执行桌面测试。
