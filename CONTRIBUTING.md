# 贡献指南

开始前先阅读根目录 `AGENTS.md`。它定义本仓库的人类与 AI 开发流程、架构边界、测试阶梯、Git 评审和发布规则。

SDK 修改放 sdk/，通用接入放 templates/desktop-app/，演示业务放 demos/workbench/。SDK 不反向依赖 Demo。保持公开接口、文档和版本一致，不使用开发机绝对路径作为构建输入。

桥接与安装器修改需补充有意义的异常输入验证。提交前完成相关配置构建及 tools/Test.ps1。Web 工程统一使用 npm 和 package-lock.json。第三方修改保留来源、补丁与授权。不要提交 out、node_modules、私钥或用户数据。

本仓库不配置远程 CI。维护者在具备完整工具链和交互桌面的 Windows 开发机执行 `tools` 下的构建、测试和验收入口，并把命令、架构、配置和结果写入 PR。自动化服务会话不能替代真实宿主验收。

## 提交流程

1. 建立或关联 Issue，写清问题和验收条件。
2. 从 `main` 创建 `feat/`、`fix/`、`docs/` 或 `codex/` 分支。
3. 按目录所有权实施；架构级变更先建立 ADR。
4. 执行与影响范围匹配的测试，更新文档和 CHANGELOG。
5. 使用 Conventional Commits，提交前运行 `git diff --check` 并审查 staged diff。
6. 创建 PR，完整填写验证、风险和回退信息；本机验证证据与评审通过后 squash 合并。
