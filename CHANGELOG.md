# 变更记录

## 0.2.0-preview.2

在内部 preview.1 验证基线上补充 Web SDK 响应结构校验，并验证业务项目从 preview.1 升级到 preview.2。

- 建立独立 GitHub 仓库维护基线：AI Agent 规范、ADR、Issue/PR 模板、CODEOWNERS、Dependabot 和四组 Windows CI。
- CI 使用无交互安全的单元/契约测试模式；真实桌面 smoke 继续作为发布前目标机验收。

- 独立 SDK、最小模板和完整 Workbench Demo 分层。
- 提供 C++ 静态 SDK、CMake 导出、TypeScript SDK 和类型产物。
- 提供会话校验 JSON 桥接及模板业务方法示例。
- 新项目生成、固定版本、完整性校验、安装升级与失败回滚。
- 整理 SOUI / Miniblink 源码、运行依赖、重编译入口及验收文档。
- 仍为预览交付，生产门槛见 docs/known-issues.md。
