# 历史 CI 验收文本的公开副本

此文件经过删减、脱敏和编码/换行规范化，不是原始字节。仅反映 2026-10-30 的本地静态、提交或拒绝记录；不验证新候选 e43a79，也不构成云端运行通过证据。

- 源逻辑标识：pr103-integration-evidence/ci-platform-preparation-20261030/preparation-report.md
- 源原始 SHA256：3e53f68f69eb9227064f36120949a944967619fa675fcd02dd8da951162099e4

# PR #103 六平台 CI 本地准备结果

已完成本地方案、九文件修改、完整补丁和静态校验。未暂存、提交、推送或触发 GitHub Actions；没有购买/启动虚拟机、修改 Actions 安全设置或上传历史日志/ZIP。

## 源码和仓库身份

- 官方仓库 chenziyang110/soui-web-sdk，GitHub 元数据 visibility=public。
- PR #103 当前 head 仍为 19d450eae04d7ee51286168712d5b60c08631e89，Draft/open/unmerged。
- 独立工作树 <SOURCE_ROOT>。
- 本地分支 codex/ci-platform-matrix-20261030；HEAD 与原索引 tree 98cb4681032285309841293d65f0041344aa5f14 未变，无暂存项/未合并项。
- 原 soui-pr103-integration 工作树仍干净，没有追 main。
- 当前 source-manifest 原 SHA256 为 41CF83F1CF060DF6842FC55F9CC5676668944057AC825519EFE20B0BB5E9AEE5。
  本轮未刷新清单；它尚不覆盖九文件 CI 修改。〔操作指引已从公开副本省略；历史结果不变。〕

## 可审阅交付

完整补丁 ci-preparation.patch：52,694 字节，
SHA256 064c72c366668ac3c7bbad51c5110be45b495dcb986ec34832a879cce4b53259。
9 files changed, 808 insertions(+), 2 deletions(-)。

- .github/workflows/platform-build.yml：六个标准 runner、固定官方 actions、最小权限、超时和 concurrency。
- tools/ci/platform-build.cjs：生成静态计划和受限 CI 执行链，Windows 无项目程序或 CTest。
- tools/ci/validate-platform-workflow.cjs：无外部依赖的工作流/计划策略校验与负面控制。
- tests/ci/installed-consumer/{CMakeLists.txt,runtime.cpp,desktop.cpp}：严格安装前缀的两消费者编译链接和公共头对象目标。
- docs/ci-platform-matrix.md：命令、依赖、费用、触发、证据边界和官方引用。
- AGENTS.md、docs/testing.md：同步旧“无远程 CI”约定，保留真实桌面与历史失败边界。

全部文件大小/SHA256 和本地状态见 local-state-and-diff.json。
工作流 SHA256 d8ea344b042c2374dd290f7b13b285e168c8c617f1bb648c10f2ce891cc76279。

## 矩阵和步骤

| runner | 目标 | Runtime 运行计划 | 所有平台均执行的编译/文件步骤 |
|---|---|---|---|
| windows-2022 | x64 | 无 | SDK build/install → 安装消费者/全公共头编译链接 → 正常模板编译链接 → stage/verify/ZIP/解压再验证 |
| windows-11-arm | ARM64 | 无 | 同上 |
| ubuntu-24.04 | x64 | 2项精确无界面测试 | 同上 |
| ubuntu-24.04-arm | ARM64 | 2项精确无界面测试 | 同上 |
| macos-15-intel | x64 | 2项精确无界面测试 | 同上 |
| macos-15 | ARM64 | 2项精确无界面测试 | 同上 |

Linux/macOS 只运行 sdk.portable.primitives 与独立前缀夹具 ci.runtime-installed。
每次运行前以 CTest JSON 只读核对唯一名称、唯一实际命令和无 fixture 依赖。
30 秒测试超时、精确正则、no-tests=error。没有 GUI、资源服务器、integration 或 symlink 测试。
Windows 完全不调用 CTest，也不运行任何编译产物、doctor/launch脚本或被阻止目标的替代程序。
Desktop consumer 和正常模板在所有平台永不运行。原 4551、consumer 原因未知和历史 FAIL 不改写。
这是六类原生架构 runner 的方案，未增加 i686/ARM32 交叉或模拟目标，不声称物理硬件覆盖。

驱动命令是 node tools/ci/platform-build.cjs --platform <windows|linux|macos> --arch <x64|arm64>。
工作区在真实 RUNNER_TEMP 下创建，新目录和过程级 vcpkg/MSBuild 隔离；缺工具/版本冲突直接失败。
Windows 以现有 vswhere+cmake capabilities 选择 VS17/18，不安装新编译器。
macOS 固定当前单一原生 slice、deployment target 12.0。
SDK 和消费者只用 Release；三个消费者目标严格绑定安装 Config/include/archives，包含此次 ABI 安装修复。
打包仅调用安装前缀的既有文件工具；application-stage/2、application-delivery/1，核 notices、二进制架构、Unix 执行位及全部解压文件 SHA256。
CI 结果只写日志/job summary；不存在 SDK ZIP 或 artifact 上传。

## 依赖和触发

官方 actions 完整 SHA 已只读核实：
checkout v7.0.1 = 3d3c42e5aac5ba805825da76410c181273ba90b1；
setup-node v7.1.0 = 949feb2413d6458794dcd2491c4babbbce0c15c1。
Node 固定 24.19.0 并显式选择 x64/arm64，cache 关闭。

Windows 官方 NuGet WebView2 1.0.4258.31 使用此前已核的固定 9,801,922字节/SHA256：
56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9。
仅数据包提取、编译链接、LICENSE/NOTICE 交付，无浏览器 Runtime 安装或包内程序执行。

Ubuntu 两架构三顶层精确 pin：
libwebkit2gtk-4.1-dev=2.52.6-0ubuntu0.24.04.1，
libgtk-3-dev=3.24.41-4ubuntu1.3，pkg-config=1.8.1-2build1。
官方 Ubuntu 签名源描述只放 runner 私有临时目录；不修改全局源、不禁签名/证书校验、不自动放宽版本。
传递依赖和 runner 基础镜像未完全冻结；本轮未安装或求解这些包。macOS 只用预装 Apple 工具和系统框架。

拟触发 pull_request(opened/synchronize/reopened/ready_for_review) 与 workflow_dispatch；
checkout 精确 PR head，权限仅 contents:read，persist-credentials=false，无 secret、pull_request_target、部署或 release。
job 45分钟、matrix max-parallel=2、同PR/分支取消旧run。
manual dispatch 仍要求工作流文件已出现在默认分支，不能保证只推 PR 分支就有手动入口。

## 费用与设置读取缺口

[GitHub 官方 runner 表](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
列出本方案六个 standard 标签；公开仓库 standard runner 计算免费，未选 larger runner。
[官方费用文档](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
说明持久存储另有额度/计量，账户剩余额度和预算未核实。初版不上传 artifact、不缓存，
ZIP 随临时 runner 消失，没有新增持久 artifact 保留；以后若批准上传，须另核额度并限 retention-days=1。
job 有公开仓库 guard，改私有后不会默认运行这份矩阵。本轮没有发生云端运行/存储操作。

未能读取的确切只读操作：
GET /repos/chenziyang110/soui-web-sdk/actions/permissions，
GET /repos/chenziyang110/soui-web-sdk/actions/workflows?per_page=100。
连接器两次返回 HTTP 400 / INVALID_ARGUMENT / “GitHub Fetch URL is not an allowed public GitHub repository or search endpoint.”
这是连接器端点限制，不是已确认的 GitHub 权限拒绝或 Actions 禁用。
未换路重试。已确认冻结 PR 源码无既有 workflow；实际启用状态、允许 action 策略、
fork 审批和存储预算仍未知，首次启用前需核对。完整回执见 github-preflight.json。

## 实际静态验证

final-static/static-commands.json 记录 9 条命令均退出 0：
两份 Node --check、一次 workflow/plan 策略校验、六个 CLI --plan。
六计划内 Windows 13阶段/0CTest，Linux/macOS16阶段/2CTest；planning spawn=0。
8 个负面控制均按预期拒绝：写权限、不安全事件、自托管runner、超时扩大、凭据持久化、
Windows CTest、未声明架构、重复参数。JSON兼容YAML可解析。
tracked diff --check=0，7个新文件差异空白检查无错误，完整补丁 reverse-apply --check=0。
首轮新增文件空白检查发现4个EOF空行，现已修正；初稿补丁/身份清单另存，
修正说明见 whitespace-correction.json。CMakeLists text=auto 的原生行尾提示为非错误，未改全局Git设置。

两名独立源码审查未发现当前静态阻断，确认公开 API/CMake target/打包参数和无隐藏应用执行。
这些不是 GitHub 服务解析、CMake配置、编译、链接或 Runtime/GUI 测试通过；
六个云 runner 均未实际运行。首次云端仍可能暴露依赖/工具链/链接问题，须保留实际失败。

releaseReady/nativeRuntimeVerified/authenticityVerified/signed 仍 false；应用 developmentOnly=true。
当前 CI 准备不解决发布版本身份，也不转授旧验收。此轮已完成可审阅本地方案，〔操作指引已从公开副本省略；历史结果不变。〕
