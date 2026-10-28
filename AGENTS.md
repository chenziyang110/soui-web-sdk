# SOUI Web SDK — AI 开发与长期维护规范

本文件是本仓库所有人类开发者、AI Agent 和自动化工具的最高优先级工程约定。修改任何代码、文档、构建、依赖或交付物前必须先阅读本文件。子目录若存在更具体的 `AGENTS.md`，只补充该目录规则，不得放宽本文件的质量、安全和验收要求。

## 1. 项目目标与当前状态

本仓库维护 Windows 平台的 SOUI + Miniblink 桌面 SDK、最小应用模板、完整 Workbench Demo、第三方源码及可重复的构建和验收工具。

- 当前交付版本为 `0.6.0`，发布材料收口中；`0.5.0` 已按限定范围预发布，生产验收未完成。不得把编译成功、短启动成功或删除 preview 标签描述为“生产就绪”。
- 生产门槛和未关闭风险以 `docs/known-issues.md` 为准，机器可读结论以 `verification/acceptance.json` 为准。
- 根目录 `VERSION` 是版本唯一来源。C++、Web SDK、锁文件、变更记录和交付清单必须与其一致。
- 支持 Windows、MSVC v143、C++17、Windows SDK 10.0.19041.0。不要静默提高最低系统或工具链要求。

## 2. 目录所有权与依赖方向

| 路径 | 所有权 | 约束 |
|---|---|---|
| `sdk/` | 公共 C++ / TypeScript SDK | 不得依赖模板或 Demo；公共行为变更必须测试并记录兼容性 |
| `cmake/` | SDK 和应用接入规则 | 不得写开发机绝对路径；必须支持源码目录整体搬移 |
| `templates/desktop-app/` | 新项目模板 | 保持最小、可独立生成和编译；不要加入 Workbench 业务能力 |
| `demos/workbench/` | 完整能力演示 | 可依赖 SDK；Demo API 不自动成为公共 SDK 契约 |
| `third_party/include,prebuilt` | 固定开发/运行依赖 | 替换时更新散列、来源、授权和两架构验证 |
| `third_party/sources/` | 可重编译依赖源码 | 不做顺手格式化；修改上游源码必须保留补丁和来源证据 |
| `tools/` | 构建、测试、安装、打包工具 | 失败必须返回非零；不得把缺失证据标为通过 |
| `tests/` | SDK 契约与安装验证 | 测行为边界、失败路径和跨组件契约，避免复制实现细节 |
| `docs/` | 面向维护者和使用者的契约 | API、构建、版本、发布或限制变化必须同步更新 |
| `verification/` | 已执行的验收证据 | 只记录真实执行结果；不得手工伪造通过状态 |

依赖方向固定为：业务页面/应用 → Web SDK / C++ SDK → Host / Runtime → SOUI / Miniblink。`sdk/` 不能反向引用 `templates/`、`demos/` 或产品业务代码。

## 3. AI 化开发流程

### 3.1 任务进入

1. 明确用户目标、影响范围、验收条件和不在本次范围内的事项。
2. 阅读相关的 `README.md`、`docs/architecture.md`、`docs/api.md`、`docs/testing.md` 和 `docs/known-issues.md`，再检查实现。
3. 先检查工作树和现有改动。用户已有改动必须保留；不得通过 reset、checkout 或覆盖文件清理工作区。
4. 为非平凡修改建立 GitHub Issue 或在 PR 中写清问题、触发条件、预期行为和验证方法。

### 3.2 设计门槛

下列变化在编码前必须新增或更新 `docs/adr/` 中的决策记录：

- 公共 C++ / TypeScript API、Bridge 协议或权限模型；
- 线程、生命周期、关闭顺序、渲染或消息泵；
- 目录所有权、SDK 包格式、升级/回退策略；
- 最低 Windows、编译器、Web 兼容目标；
- 新增或替换核心第三方依赖。

ADR 必须说明背景、决定、备选方案、兼容性、迁移和验证。小型局部修复不需要为了形式新增 ADR。

### 3.3 实现规则

- 优先最小、可审查的改动；避免在功能修改中夹带大范围重命名和格式化。
- 输入在系统边界校验：JSON 类型/大小/深度、路径、origin、session、token、超时和权限。
- 不在 UI、引擎或 Bridge 线程执行无界阻塞操作；共享状态必须有明确同步和生命周期。
- 适配器必须比宿主存活更久；导航撤销旧会话；关闭顺序不得绕过现有契约。
- Web 页面按 Chrome 49 兼容目标构建；不得直接依赖未经转译的现代语法或 Web API。
- 不硬编码密钥、令牌、证书、用户名或开发机绝对路径。使用环境变量只传入秘密，不提交值。
- 不编辑 `out/`、`dist/`、`node_modules/` 或生成的 Web `dist/` 作为源码修复。
- Web 工程统一使用 npm 与 `package-lock.json`；不要并存其他包管理器锁文件。依赖大版本升级必须单独规划并完成兼容性验证。
- 不直接修改预编译 DLL/LIB。依赖升级必须从可追溯来源重建或导入，并更新清单。

### 3.4 验证阶梯

本次 0.6.0 采用维护者 2026-10-28 的最新明确授权：包含本地受控 DevTools 的普通 GitHub Release，低收益长测和重复演练后置；限定范围与真实状态见 docs/releases/0.6.0.md 和 ADR-0136。GitHub 分类不改变 releaseReady=false，不删除未完成资格或保留失败；原权限、资源 fence、超时和实际退出判定保持。


按影响范围执行最小充分集合；发布前执行完整集合。

0.5.0 已发布交付采用的限定验证范围以 `docs/releasing.md` 的“0.5.0 本次发布范围”为准。维护者随后于 2026-10-16 明确要求解决剩余 Issue，并恢复按各 Issue 关闭条件执行真实 8 小时等长测；该授权适用于后续开发验证，不追溯修改 0.5.0 的历史结果。仍不得将未执行项、既有失败或生产门槛改写为通过。

1. Web SDK：`npm --prefix sdk/web ci`、`npm --prefix sdk/web run build`、`node --test tests/web/sdk-client.test.mjs`。
2. SDK/安装器：`node tests/sdk/sdk-package-smoke.cjs`。
3. 本机单架构：`tools/Build.ps1` 后执行 `tools/Test.ps1`。
4. 公共 API/CMake：SDK 独立安装构建 + `tests/package-consumer/` 外部引用。
5. 模板/部署：生成到仓库外目录，独立构建并运行 `--smoke-test`。
6. 发布候选：x86/x64 × Debug/Release 四组；Release 做真实模板和 Demo 启动。
7. 底层依赖变化：两架构执行 `Build-Dependencies.ps1 -Component All`，再执行 `Test-Dependencies.ps1`。
8. 汇总：`node tools/Collect-Verification.cjs`。只有该工具成功且证据齐全，才可更新验收结论。

本仓库不配置远程 CI。维护者必须在具备完整工具链的 Windows 开发机执行适用验证，并在 PR 中记录命令、架构、配置和结果。真实窗口、输入、DPI、多屏、长期内存和关闭稳定性必须在可交互目标机执行。

0.3 的 build/smoke 记录必须绑定源码、构建 ID 和完整运行载荷，不能用旧 v1 成功标记授权发布。`--foundation` 只验收基础切片，不代表完整候选。完整源码清单从 Git 暂存区生成：先完成报告和文档、暂存修改、刷新并验证清单，最后打包。源码打包不能与写入验收文件的步骤并行。应用输出的 `resources/` 为部署工具所有，用户数据必须存放在其他目录。

### 3.5 完成定义

任务完成必须同时满足：

- 目标行为已实现，失败路径有清晰错误；
- 相关测试和构建实际通过，结论中列出未执行项；
- API、配置、目录或操作流程变化已更新文档；
- 公共变更已更新 `CHANGELOG.md`，版本变化已同步所有版本入口；
- `git diff --check` 通过，无秘密、构建产物或开发机路径进入提交；
- 不扩大 `releaseReady`、兼容性、性能或安全声明；
- PR 描述包含问题、最终行为、验证证据、风险和回退方式。

## 4. Git 与评审规范

- 默认分支：`main`。日常修改通过分支和 PR，分支使用 `codex/<主题>`、`feat/<主题>`、`fix/<主题>` 或 `docs/<主题>`。
- 提交使用 Conventional Commits：`feat:`, `fix:`, `docs:`, `test:`, `build:`, `ci:`, `refactor:`, `chore:`。
- 一个提交表达一个可解释意图；不要提交无关文件。提交前审查 staged diff。
- 不重写已共享历史，不强推受保护分支，不绕过必需检查。合并优先 squash，PR 标题作为最终提交标题。
- 公共 API、协议、权限、线程模型、第三方二进制和发布流程修改至少需要一名维护者批准。
- 发现安全问题遵循 `SECURITY.md`，不要在公开 Issue 暴露可利用细节或秘密。

## 5. 版本、依赖与发布

- 遵循语义化版本。preview 期间破坏性变化仍需在变更记录和迁移说明中明确标注。
- 同一版本号禁止对应不同 SDK 内容；安装器会拒绝这种情况。内容变化必须增加版本。
- 依赖变化更新 `third_party/manifests/dependencies.json`、相关 `SOURCE-MANIFEST.json`、SBOM、授权说明和散列。
- 发布顺序：完成验证 → 更新版本/变更记录 → 生成 SDK/源码/运行包 → 校验 SHA-256 → 创建预发布 GitHub Release。
- 正式版还需关闭适用生产门槛、完成授权审核、安装/升级/回退及目标系统矩阵，并如实声明签名状态。按维护者 2026-10-09 的指示，0.5.0 可未签名交付；取得签名材料后另验签名包与信任链。

## 6. 文档与事实边界

- 使用中文说明业务和维护流程，代码标识、命令和标准名称保持原文。
- 文档中的命令必须能从仓库根目录执行；路径一律使用相对路径。
- “已验证”必须绑定当前版本、架构、配置和证据；历史实验只能描述为历史结果。
- 性能对标必须使用同设备、同负载和可重复测量。没有帧时间、输入延迟、内存和稳定性数据时不得声称达到 Electron 水平。

## 7. Agent 行为约束

- 先读后改，优先使用 `rg` 定位；修改文件使用可审查的补丁。
- 可逆、任务内的实现和验证无需反复请求确认；公开发布、删除远端数据、改仓库可见性、强推等重大动作必须有明确授权。
- 遇到失败先定位根因，不能通过删除测试、放宽断言、跳过错误或伪造证据让流水线变绿。
- 完成汇报以结果为先，说明变更、验证、风险和剩余门槛；不得隐藏失败或把计划表述成已完成。

## 8. 常用入口

- 环境检查：`powershell.exe -File tools/Doctor.ps1`
- 日常构建：`powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release`
- 单元/契约测试：`powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release -UnitOnly`
- 真实桌面验证：`powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release`
- 生成项目：`powershell.exe -File tools/New-Project.ps1 -Name MyApp -OutputRoot ..\MyApp`
- 底层重编译：`powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x64 -Component All`
- 依赖运行验证：`powershell.exe -File tools/Test-Dependencies.ps1 -Architecture x64`
- 验收汇总：`node tools/Collect-Verification.cjs`
- 打包：`powershell.exe -File tools/Package.ps1`

更详细说明见 `README.md` 和 `docs/`。
