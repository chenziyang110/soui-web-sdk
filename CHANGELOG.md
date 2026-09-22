# 变更记录

## 0.3.0-preview.1（未发布：基础切片）

- 恢复被上游忽略规则遗漏的 25 个 SOUI/Skia/JSON 源码与工程文件。
- 完整源码清单升级为 v2，区分文本换行与二进制身份，支持干净 Git checkout 与无 Git 解压验证，发布先暂存再核验。
- SDK v1 包新增可选源码换行规范，安装内容继续严格锁定字节；生成项目保护 SDK 文件免受 Git 换行转换。
- 构建/启动/打包绑定源码、构建 ID 和完整运行载荷；拒绝旧记录、缺失场景、等长资源修改及无依据 SkipWeb。
- 重新部署替换旧 Web 资源；模板与 Demo 独立打包并输出大小/散列报告。
- 保留私有仓库与本机验证。此次为 0.3 的首个基础切片，完整 Electron 性能对照、异步 Bridge、标准桌面模块与多窗口仍未交付。

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
