# PR #103：Win32 Desktop SDK 安装、消费者与正常模板链接验证

> 归档说明：这是旧 source tree `98cb4681032285309841293d65f0041344aa5f14` 的脱敏历史 Windows Release build-only 记录。原文“当前”“本轮”“未提交”等以记录当时为准，不验证 2026-11-02 恢复候选（标识前缀 `e43a79`）。原始技术结果、工具退出码和 SHA 保留；内部记录已删节。

完成：历史固定候选的 Desktop SDK 构建/独立干净安装、严格安装前缀消费者编译链接、安装版生成器生成原模板、正常模板编译链接全部通过。8 个实际阶段均 exit 0。此前缺少官方 WebView2 SDK 的前置阻塞在取得固定官方包后解决。没有执行任何生成的 EXE、CTest 或 GUI，结果为 build-only。

## 固定源码、输出及工具

- 源码：`<SOURCE_ROOT>`
- 索引 tree：`98cb4681032285309841293d65f0041344aa5f14`
- HEAD：`9e4e9c1fdeacc0eae8318a1bf40a04e8c1da97e7`
- 固定 main / MERGE_HEAD：`0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`
- 候选仍已暂存、未提交；本轮未修改源码或索引，末轮 diff 比较返回 0，无未合并、未暂存或未跟踪文件。未追更新 main。
- 源码清单 SHA256 仍为 `41cf83f1cf060df6842fc55f9cc5676668944057ac825519efe20b0bb5e9aee5`，未重跑清单生成/校验。SDK 版本仍为未发布候选 0.6.0，releaseReady=false。
- 新输出根：`<WORKSPACE>\windows-win32-desktop`；deps 为私有依赖，db 为 SDK 构建，di 为干净安装，cb 为消费者构建，template-src/tb 为正常生成模板及构建。
- 目标：Win32/x86、Release；宿主 <HOST> / AMD64 Windows。未提供 32 位硬件原生运行证明。
- 工具：VS2022 BuildTools，CMake 3.31.6-msvc6，MSBuild 17.14.60.43110，MSVC 19.44.35229.0 / Tools 14.44.35207 / Hostx64-x86，Windows SDK 10.0.19041.0；PowerShell 7.6.5；安装版纯文件工具使用已有 Node 24.19.0（身份见 node-tool.json）。

旧 windows-win32-runtime 的 Runtime 安装/链接验收没有重跑。新的正常 Desktop SDK 项目按其依赖关系构建了所需 Runtime 静态库；没有把旧库复制成新构建结果，也没有启动旧或新程序。

## 官方固定包来源和完整许可

版本要求来自 `sdk/renderers/webview2/dependency.json`，为 Microsoft.Web.WebView2 **1.0.4258.31**，与既有项目验收版本一致。

- [官方版本页面](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31)
- 下载 URL：`https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4258.31/microsoft.web.webview2.1.0.4258.31.nupkg`
- HTTP 200；直接官方 HTTPS、无重定向；没有代理或镜像路线。
- 包大小 9,801,922 bytes；SHA256 与仓库 pin 完全匹配：`56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9`。
- 135 个 ZIP 成员作为数据解压到 `windows-win32-desktop/deps/webview2-1.0.4258.31`；预先检查成员路径均落在新私有目录，没有创建链接、导入 NuGet props/targets 或执行包内程序。没有安装浏览器 Evergreen Runtime。
- nuspec：ID/版本匹配，license type=file 指向 LICENSE.txt，`requireLicenseAcceptance=false`。LICENSE 与旧同版本件只差末尾一个 LF，NOTICE 逐字节相同。

本轮保留包、nuspec、LICENSE、NOTICE 及下载/解压记录。官方材料的原始字节：

| 包材料 | bytes | SHA256 |
|---|---:|---|
| LICENSE.txt | 1,487 | `0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55` |
| NOTICE.txt | 3,894 | `106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90` |
| build/native/include/WebView2.h | 2,939,599 | `dff1e3181ec7ec203a34ef6eefa966590e0ef0ba1a5c3fe3b69da6508c2f8a02e` |
| build/native/x86/WebView2LoaderStatic.lib | 11,428,262 | `bd66d0b41302e6d56938e61ded16e32bc66318fd54324b0dae419aeeccfb9b96` |

完整机器记录见 `sdk-extraction.json`。LICENSE/NOTICE 安装后保持这些原始字节。

## 构建链审查与过程隔离

Desktop ON，Tests、Desktop Example、legacy Template/Demos/WebView2/UIA/Vendor/AutoHost/InputUI、Desktop startup trace 均 OFF。Windows Desktop 分支仅定位私有官方包的 header、x86 静态 loader 和许可材料；可达链没有 try_run、项目产物执行、NuGet restore、vcpkg 或浏览器 Runtime 安装。标准编译器识别、架构及 ABI/atomic 检查只编译/链接，不运行生成物。

生成后的 Desktop/consumer/app 项目没有 PreBuild/PreLink/PostBuild 应用事件。官方 CMake stamp 检查保留。安装脚本只有文件安装与导出处理，目标 prefix 显式为 windows-win32-desktop/di，DESTDIR 在过程内清除。

沿用经过审查的驱动，在首次 project() 前清除继承 CMAKE/VCPKG/编译器 launcher、flags、导入 hook、Node preload；固定 VS instance、Win32/v143-host-x64 和 SDK。UserRootDir、MSBuildUserExtensionsPath、VCLibPackagePath、VcpkgRoot 指向本轮空任务目录，禁用 manifest/artifacts integration、Directory.Build 和 user wildcard 导入；在环境、CMake globals、MSBuild /p 三处设置。精准构建均 `--parallel 1 /m:1 /nodeReuse:false /noAutoResponse`。未修改任何全局配置、包源或安全设置。

原模板正常目标执行安装版 `sync-desktop-assets.cjs`。该工具使用 fs/path/crypto 进行文件处理，无 subprocess、网络、GUI、系统设置或链接创建。本轮同步新 template-src/web 到新 tb/Release/web；文件处理限定在这些新目录及同步工具拥有的文件。不构建 stage、verify_stage、archive_stage 目标。

首次静态 helper 路径字符串断言失败，当时未发起模板构建。实际路径含合法的 `di/lib/cmake/SouiDesktop/../../../share/...`；规范化核对后确认精确指向安装版 helper。纠正路径审查后按原命令完成模板构建；该记录不是编译失败。

## 实际执行命令与结果

下表为实际执行核心命令和真实退出码摘要。原始机器命令与日志未随本公开归档复制，机器路径以稳定占位符表示。

| 阶段 | 核心参数 | exit |
|---|---|---:|
| desktop-configure | `-S <candidate> -B windows-win32-desktop/db`，Desktop ON，官方私有 SDK_ROOT，Tests/Example OFF | 0 |
| desktop-build | `--build windows-win32-desktop/db --config Release --target soui_desktop --parallel 1` | 0 |
| desktop-install | `--install windows-win32-desktop/db --config Release --prefix <task>/windows-win32-desktop/di` | 0 |
| consumer-configure | `-S windows-win32-desktop/consumer-src -B windows-win32-desktop/cb`，Desktop/Runtime_DIR 和 PREFIX 仅指 di | 0 |
| consumer-build | `--build windows-win32-desktop/cb --config Release --target desktop_installed_linkcheck --parallel 1` | 0 |
| template-generate | 安装版 `create-desktop-app.cjs --output windows-win32-desktop/template-src --name InstalledDesktopCheck --identifier org.soui.buildonly --version 0.1.0` | 0 |
| template-configure | 原生成项目 `-S windows-win32-desktop/template-src -B windows-win32-desktop/tb`，仅 di 前缀，APP trace OFF | 0 |
| template-build | `--build windows-win32-desktop/tb --config Release --target InstalledDesktopCheck --parallel 1` | 0 |

原有编译警告及兼容变量未使用警告均保留；没有改变源码或放宽断言来获得成功。

## 干净安装和真实来源核验

安装共 **62 文件**，全部 install_manifest 路径在新 di 前缀内。

- **22 个公共头**全部与当前候选源码逐字节、SHA256 一致，包含修复的 view_observation_abi.h。
- Desktop/Runtime 库均与本轮生产库逐字节相同；安装 loader 与官方包逐字节相同。
- WebView2 的 LICENSE/NOTICE、Runtime 的 LICENSE.md/THIRD_PARTY_NOTICES.md 全部与各自来源逐字节相同。
- 安装的工具、模板和 frontend 共 23 个文件均匹配候选来源。生成的 17 个文件符合安装模板的三项 token 替换及原始 frontend 复制，没有手工删改原模板功能。
- 正常模板所同步的 **8 个 web 资产**与生成源码及 ownership manifest 的 size/hash 全部相同。

安装库身份：

| 库 | bytes | SHA256 |
|---|---:|---|
| soui_desktop.lib | 3,149,778 | `cb320d0fdd208ff3235786139a9439b54bfc3f3c6b680bbda7b45c58824c41c1` |
| 本轮所需 soui_runtime.lib | 6,715,686 | `3b2f30207c889b313e10e1ba4f77ee84ea4115e72d91cf2822c1f394dee06fbe` |
| WebView2LoaderStatic.lib | 11,428,262 | `bd66d0b41302e6d56938e61ded16e32bc66318fd54324b0dae419aeeccfb9b96` |

消费者只使用 `find_package(SouiDesktop 0.6.0 EXACT ... NO_DEFAULT_PATH)` 及 di 的 Runtime，严格断言 Config、include、Desktop/Runtime archive 和 loader 路径。main 只用于编译链接真实 Desktop 构造/析构与 Runtime 符号，不执行 initialize/run。实际 compiler dependencies 为143项，其中7个项目头全部来自 di/include；源码或旧前缀回退为0。

原模板实际 CL.read tlog 确认 app.h 来自 di/include，其余模板本地头来自生成目录。两项目的 link.command 和 **link.read** 都确认三份库来自 di/lib，旧 windows-win32-runtime/ri、producer db 或未安装 deps 库回退为0。并非只检查 CMake 声明或链接命令文本。特定 WebView2 入口名未在 stdout 记录中检出，不据此额外声称某个入口已被调用；链接库读取与完整链接成功由实际 tlog/产物证明。

证据文件：`install-identity.json`、`template-source-identity.json`、`managed-assets-identity.json`、`consumer-dependency-identity.json`、`link-and-template-provenance.json`、`actual-link-reads.json`，以及构建目录中的原始 dependencies/tlog。

## 仅解析字节的产物身份

两份 EXE 均 I386 0x014c / PE32 0x010b / Console 3；没有加载执行。

| 产物 | bytes | SHA256 |
|---|---:|---|
| desktop_installed_linkcheck.exe | 245,248 | `62c17010b8e59d269462a6414273fe9c49a912ac6a6268610acfbdf3ce9db8d3` |
| InstalledDesktopCheck.exe | 487,424 | `8a3e4d0c3e1be6f6c95949f2d9d5489e4800e5f3322c549f6b03259791acd42b` |

## 保留边界

本轮没有当前候选的 runtime/GUI/生命周期/跨架构运行结果，没有应用 PID、输出或退出码。8个 exit 0属于下载后的工具/生成/构建过程，不是应用退出码。完整 Desktop **build-only** 安装/链接缺口已关闭；运行和发布门槛仍未关闭，其他平台或架构不由本次 Win32 构建覆盖。

7 项历史证据与产物的散列保持不变。旧 primitives 的明确 4551 技术错误与旧 consumer 底层原因未知状态均保留；未构建或启动 primitives。新产物未运行，不能推断其运行结果或是否出现同一错误。

此阶段未安装浏览器 Runtime 或变更全局包源。更早缺少 SDK 的结论仅为当时阶段状态，本报告记录取得固定官方依赖后的 build-only 结果。
