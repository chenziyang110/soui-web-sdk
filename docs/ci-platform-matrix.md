# 跨平台 Runtime / 统一 Desktop CI

此工作流从 PR #103 的固定提交 19d450eae04d7ee51286168712d5b60c08631e89
准备，原 source tree 为 98cb4681032285309841293d65f0041344aa5f14。
当前仅完成本地源码和静态校验，尚未提交、推送、触发或取得任何云端通过结果。
工作流文件采用 JSON 兼容的 YAML，以 Node 内置 JSON parser 静态校验，不安装额外解析器。

## 六个标准 runner 与边界

| runner 标签 | 编译目标 | Runtime 测试 | Desktop / 正常模板 |
|---|---|---|---|
| windows-2022 | x64 | 不注册、不运行 | build / install / compile / link / 文件打包 |
| windows-11-arm | ARM64 | 不注册、不运行 | 同上 |
| ubuntu-24.04 | x64 | 两项原生无界面 Runtime 测试 | 同上，不启动应用 |
| ubuntu-24.04-arm | ARM64 | 同上 | 同上 |
| macos-15-intel | x64 | 同上 | 同上 |
| macos-15 | ARM64 | 同上 | 同上 |

这里的原生指 runner 的 OS/Node/编译目标架构相符，不表示已经取得物理硬件证据。
不是 universal2、Rosetta、QEMU 或 x86 兼容执行矩阵。此次没有增加 Linux i686、
ARM32 交叉编译/模拟，也没有 Windows x86 job；这些须后续单列，不能转称原生验收。

Windows 的限制保留本地应用控制边界：不以云端测试运行此前受阻目标或等价程序，
不调用任何 CTest、模板、consumer、doctor、launch 或 GUI 程序。没有签名、解除阻止、
安全保护禁用或策略修改。旧 primitives 的 NativeErrorCode 4551、旧 consumer 原因未知
及其他历史 FAIL 保留，不能用本工作流的编译结果消除。

Linux/macOS 仅运行根构建内的 sdk.portable.primitives，以及独立安装消费者目录内
新增的 ci.runtime-installed。两者无 GUI、网络监听、剪贴板、热键、用户数据或符号链接操作；
权限 token 使用已有系统 CSPRNG，Runtime 还使用单调钟。CTest 使用精确正则、
30 秒测试超时、无测试即失败，并在运行前只读核对选中测试、真实命令与无 fixture 依赖。
不执行 integration、resource-server、整个 test preset 或 no-gui 标签集合。

## 每个 job 的顺序与命令

唯一驱动入口如下，平台和架构由固定矩阵给定：

~~~sh
node tools/ci/validate-platform-workflow.cjs
node tools/ci/platform-build.cjs --platform linux --arch arm64
~~~

实际执行仅限匹配的 GitHub runner 和本仓库。默认输出在真实 RUNNER_TEMP 下的新 sc 目录，
不覆盖已有输出。Windows 通过现有官方 vswhere 识别目标组件和 VS major，再核对现有 CMake
是否支持相应 VS17/VS18 generator；缺组件或不匹配即失败，不下载编译器、不回退架构。
Windows ARM 官方镜像文档存在 VS2022/VS2026 映射差异，所以不固定某一旧安装路径。
这不提高 SDK 对其他使用者的最低工具链要求。

1. 全新 SDK 构建：SOUI_BUILD_PORTABLE=ON、SOUI_BUILD_DESKTOP=ON，关闭 example、
   legacy template/demo/WebView2/UIA/vendor/auto-host/input UI 和 startup trace。
   Windows SOUI_BUILD_TESTS=OFF；Linux/macOS ON 但仅构建 soui_desktop 和 primitives 精确目标。
   所有构建为 Release，最多两个编译作业，不调用 ALL_BUILD。
2. Linux/macOS 仅编译并运行 sdk.portable.primitives；Windows 无此步骤。
3. 安装 Runtime/Desktop SDK 到新私有前缀；不使用源树 include 回退。
4. tests/ci/installed-consumer 使用 0.6.0 EXACT、NO_DEFAULT_PATH，断言两个 Config、
   单一公共 include、Release 静态库、Windows WebView2 loader 全在该前缀。
   编译链接 ci_runtime_consumer、ci_desktop_consumer，并编译全部安装公共头对象。
   runtime_metrics.h 与 view_observation_abi.h 是显式必需输入，覆盖本次漏装修复。
5. Linux/macOS 仅运行 ci.runtime-installed；Desktop consumer 永不运行。
6. 用安装前缀里的 create-desktop-app.cjs 生成未改写的统一模板 CiDesktopCheck，
   从同一安装前缀配置并链接；再核对模板缓存中实际包来源。macOS 固定单一原生 slice、
   deployment target 12.0；Windows 原有 vcpkg/MSBuild 用户导入按 job 进程隔离。
   CPATH/INCLUDE/LIB 等编译器环境搜索路径及 PKG_CONFIG 环境覆盖会被清理；预装工具、
   系统 SDK 和包依赖仍按原生规则解析。当前只证明未配置源码回退，实际编译依赖留待云端记录。
7. 使用安装版 stage / verify / archive 工具生成 application-stage/2 应用目录及
   application-delivery/1 ZIP。只传当前平台需要的首方 Runtime 和 WebView2 notices。
8. 在新目录解压 ZIP，再 verify，并逐文件比较 stage/解压树的类型与 SHA256。
   verifier 另外检查二进制架构、notices 和 Unix 执行位；不运行包内文件。
   最后在 job summary 写命令退出状态、源码 commit、runner/ImageVersion、工具版本、
   ZIP 字节数和 SHA256。没有 SDK 前缀 ZIP 或历史本地日志上传。

每条 configure/build/install/file-tool 命令有独立超时；完整 job 45 分钟。
标准 runner 标签不会冻结镜像中的 CMake、MSVC、GCC、Apple Clang、系统 SDK 版本；
首次云端运行仍可能暴露真实配置/链接问题，应保留失败并修复，不能静态宣称通过。

## 官方依赖与固定身份

- actions/checkout v7.0.1：3d3c42e5aac5ba805825da76410c181273ba90b1。
- actions/setup-node v7.1.0：949feb2413d6458794dcd2491c4babbbce0c15c1。
  工程 Node 24.19.0，架构显式取 x64/arm64，自动 package-manager cache 关闭。
- Windows 仅下载官方 Microsoft.Web.WebView2 1.0.4258.31 NuGet 数据包：
  SHA256 56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9，
  9,801,922 字节；只解压、编译链接与交付 LICENSE/NOTICE，不安装浏览器 Runtime或执行包内程序。
- Ubuntu 24.04 两架构固定 libwebkit2gtk-4.1-dev=2.52.6-0ubuntu0.24.04.1、
  libgtk-3-dev=3.24.41-4ubuntu1.3、pkg-config=1.8.1-2build1。
  使用 RUNNER_TEMP 内的源描述和现有 Ubuntu archive 签名 keyring，仅 Ubuntu 官方
  noble / noble-updates / noble-security。缺版本、依赖冲突或签名问题直接失败，
  不放宽到 latest、不降低校验、不修改全局源。记录 apt policy / dpkg / pkg-config 版本。
  这只锁三个顶层包，未锁完整传递依赖或基础镜像；本轮没有实际求解/安装验证。
- macOS 使用预装 Apple 工具与系统框架，不运行 Homebrew 安装。

## 触发、权限、费用与当前缺口

首次只监听 push.branches 精确分支 codex/portable-runtime-platform-matrix-20261020，
checkout 精确 github.sha。移除 pull_request 触发，单次匹配 push 对这份工作流只生成一轮
六项矩阵；错误分支和 tag push 不触发，也不会同时再生成一轮 PR 事件检查。
GitHub 官方说明有合并冲突时 pull_request 不运行；因此此次不依赖 PR 可合并状态。
保留 workflow_dispatch 供将来使用，它仍有默认分支工作流存在的前提，当前不依赖手动入口。
这不是通用 fork PR 检查；不使用 pull_request_target、不 rebase、不改 main，也不更改
账号、仓库或分支安全设置。

顶层权限仅 contents:read，checkout 不持久化凭据，无 secret、自托管服务或写权限。
矩阵最多同时两个 job，同一分支 concurrency 取消旧 run。
仅公开仓库允许启动该 job；若改私有，先核对预算和额度，不默认同意计费。
chenziyang110/soui-web-sdk 当前由 GitHub 元数据确认为 public，公开仓库标准 runner
计算免费，未选 larger runner。Artifact/Packages 共享存储额度仍可能计费，账户剩余额度
未核实，因此初版不上传 artifact、不缓存，ZIP 随临时 runner 生命周期消失。
没有新增持久 artifact 保留；若以后批准上传，另核预算并限制 retention-days=1 与文件集合。

GitHub 连接器读取 Actions permissions 和 workflows 列表的 URL 被其端点 allowlist
以 INVALID_ARGUMENT 拒绝，不能据此推断 Actions 已禁用或账号无权限。只确认冻结 PR
源码没有既有 workflow；实际 Actions 启用状态、允许 actions 策略、fork 审批和存储预算未知。
后续首次启用前须确认这些设置；不为此绕过端点或改安全权限。

Linux/macOS 两项 Runtime 通过也不证明 Desktop/GUI 运行。应用 nativeRuntimeVerified、
authenticityVerified、signed、dependenciesBundled 保持 false，developmentOnly=true；
源码资格 releaseReady=false。stage/delivery schema 本身没有 releaseReady 字段。
发布前仍须协调 0.6.0 新内容版本身份，旧验收只作为原身份的历史记录。

## 本地静态检查

~~~sh
node --check tools/ci/platform-build.cjs
node --check tools/ci/validate-platform-workflow.cjs
node tools/ci/validate-platform-workflow.cjs
node tools/ci/platform-build.cjs --platform windows --arch arm64 --plan
~~~

--plan 只拼接路径和参数，不发现工具、spawn、下载、配置或构建。静态校验解析
JSON/YAML、核对六标签/固定 actions/权限/超时/费用约束、检查全部六个平台计划；
13 个负面场景拒绝内容写权限、PR/不安全触发器、错误/通配分支和 tag 订阅、旧 checkout
回退、自托管标签、超时放大、凭据持久化、Windows CTest、未声明架构及重复参数。
另以纯配置模型核对正确分支只选一轮六项矩阵，错误分支/PR/tag/私有仓库不选任务，
默认分支工作流缺失时不能依赖 dispatch。该模型不是 GitHub 实际触发证据。
另检查14个注入的环境覆盖会被清理，同时保留
原生工具 PATH 和系统 SDKROOT。这不是 GitHub 服务解析、CMake 配置或六平台实测。
本地收尾按项目现有流程从完整暂存区生成并校验 source-manifest；实际 tree、清单哈希和
退出码另存任务收尾报告。清单只声明对应索引的文件和哈希，不代表配置、编译或云端验收。
提交、推送及触发仍待审阅，不把旧清单当作包含新增 CI 文件的完整清单。

## 官方资料（2026-10-30 只读核对）

- [标准 runner 标签及架构](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
- [Actions 费用和存储](https://docs.github.com/en/billing/concepts/product-billing/github-actions)
- [工作流语法](https://docs.github.com/actions/using-workflows/workflow-syntax-for-github-actions)
- [触发与手动工作流边界](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows)
- [runner 镜像清单](https://github.com/actions/runner-images/blob/main/README.md)
- [Windows ARM VS2026 清单](https://github.com/actions/runner-images/blob/main/images/windows/Windows11-VS2026-Arm64-Readme.md)
- [CMake VS2026 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)
- [checkout 固定源码](https://github.com/actions/checkout/blob/3d3c42e5aac5ba805825da76410c181273ba90b1/action.yml)
- [setup-node 固定源码](https://github.com/actions/setup-node/blob/949feb2413d6458794dcd2491c4babbbce0c15c1/action.yml)
- [Node 24.19.0](https://nodejs.org/en/blog/release/v24.19.0)
- [WebView2 固定包](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31)
- [Ubuntu WebKitGTK 包](https://packages.ubuntu.com/noble-updates/libwebkit2gtk-4.1-dev)
- [Ubuntu GTK 包](https://packages.ubuntu.com/noble-updates/libgtk-3-dev)
- [Ubuntu pkg-config 包](https://packages.ubuntu.com/noble/pkg-config)
