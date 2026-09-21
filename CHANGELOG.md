# 变更记录

## 0.2.0-preview.2

在内部 preview.1 验证基线上补充 Web SDK 响应结构校验，并验证业务项目从 preview.1 升级到 preview.2。

- 建立独立 GitHub 仓库维护基线：AI Agent 规范、ADR、Issue/PR 模板、CODEOWNERS、Dependabot 和本机验收流程。
- 按维护决策不配置远程 CI；PR 必须记录本机单元、契约和真实桌面验证证据。
- Dependabot 按模块合并兼容的小版本与补丁更新，大版本升级必须经过显式规划和验证。
- 升级模板和 Demo 的 Vite/PostCSS 构建链，统一 npm 锁文件并清零当前 npm audit 告警。

- 独立 SDK、最小模板和完整 Workbench Demo 分层。
- 提供 C++ 静态 SDK、CMake 导出、TypeScript SDK 和类型产物。
- 提供会话校验 JSON 桥接及模板业务方法示例。
- 新项目生成、固定版本、完整性校验、安装升级与失败回滚。
- 整理 SOUI / Miniblink 源码、运行依赖、重编译入口及验收文档。
- 仍为预览交付，生产门槛见 docs/known-issues.md。
