# ADR 0146：Desktop 应用暂存的固定 notice 交付

状态：本地实现；不代表发布许可或完整第三方依赖审计。

## 决策

应用 stage 升级为 `soui.desktop.application-stage/2`。固定复制 SDK 根
`LICENSE.md` 和 Portable Runtime 的 `THIRD_PARTY_NOTICES.md`；Windows
另复制实际链接的 WebView2 SDK 包的 `LICENSE.txt`、`NOTICE.txt`。沿用现有
MIT 原文，不作新的许可选择。CMake 从源树目标或可搬迁安装包显式传入来源；
直接 CLI 调用者必须提供与构建一致的文件，不搜索其他版本或 legacy 依赖树。

Windows/Linux 使用 `legal/`；macOS 使用
`<name>.app/Contents/Resources/legal/`，与可服务的 `web/` 分离。
固定名称为 `LICENSE.md`、`THIRD_PARTY_NOTICES.md`，Windows 追加
`WebView2-LICENSE.txt`、`WebView2-NOTICE.txt`。输入须非空普通文件且无符号链接祖先，
拒绝重叠、同一来源重复使用及可检测的输入变化。按原始字节复制并记录 SHA-256，
保留 create-only 和 manifest-last；失败的部分目录由调用者检查，不自动删除。
输入仍须可信且在暂存期间不可变；这些检查不构成抵抗并发恶意写入的文件系统沙箱。

v2 manifest 的 `notices` 使用 `soui.desktop.notices/1` 固定平台集合，文件大小和
哈希仍在 `files` 中。verifier 拒绝缺失、空文件、篡改、错平台和集合外路径。
兼容读取 v1 仅表示旧完整性契约成立，明确不宣称 notice 已交付。
ZIP 沿用逐 manifest 文件归档，不增加任意目录收集。

## 验证边界

2026-10-28：Windows Node 24.14 的 stage/verify/archive 定向脚本共 32 项，
25 通过、7 项按 POSIX 条件跳过；没有尝试 Windows 符号链接创建。Ubuntu 22.04
默认用户使用任务目录内的官方 Node 24.22.1 Linux x64，32/32 通过且无跳过；
符号链接、大小写及 POSIX 权限夹具只使用 Linux 临时目录。官方 HTTPS 发布包的
SHA-256 与发布页及下载校验清单一致；未声称完成 PGP 签名验证，也未改全局工具链。

CMake 验证使用真实源 SDK 配置、生产生成的 Config 与搬迁安装布局元数据夹具，
按 Debug/Release 暂存固定 notice 并归档；输入为合成 PE，未执行应用。此类夹具
验证来源连接和交付契约，不能代替完整安装 SDK 的重新编译、链接或运行验收。
这些结果均不等于 GUI、签名、公证或法律放行。

同日结构化自审修正两个边界：notice 在打开前检查普通文件并使用非阻塞打开，
避免 FIFO 等待写端；安装 WebView2 来源检查覆盖消费配置及显式配置映射，不能
仅依赖 `IMPORTED_CONFIGURATIONS` 列表。CMake 夹具确认未列出的 Release location
确实会被选用，修复后已列出、未列出、映射到其他包的配置均被拒绝。
FIFO 回归仅在 Linux 临时目录执行；此记录是自审与定向回归，不是独立审查结论。
