# PR #103：Windows 分架构编译、链接与 v2 文件交付验证

> 归档说明：这是旧 source tree `98cb4681032285309841293d65f0041344aa5f14` 的脱敏历史 Windows Release build-only 记录。原文“当前”“本轮”“未提交”等以记录当时为准，不验证 2026-11-02 恢复候选（标识前缀 `e43a79`）。原始技术结果、工具退出码和 SHA 保留；内部记录已删节。

已完成 Win32 已有模板的新增文件交付验证，以及现有 x64 工具链能够完成的 Runtime/Desktop SDK、安装后消费者、正常模板和 v2 ZIP 全流程。全程没有应用、CTest、doctor、launch 或 GUI 执行。ARM64 缺 MSVC 目标工具和 VC 库，未尝试配置或构建，也没有以其他架构替代。

## 状态矩阵

| 架构 | SDK / 安装 / 消费者 / 模板 | v2 stage / verify / ZIP / 解压核验 | 运行 |
|---|---|---|---|
| Win32 / x86 | 沿用 windows-win32-desktop 已完成的8阶段 build-only 证据，本轮不重建 | 新增4个工具阶段全部 exit 0；16文件逐项匹配，解压 verifier exit 0 | 未执行 |
| x64 | Runtime/Desktop 构建安装、Runtime独立消费者、21头OBJECT、Desktop消费者、安装版正常模板均通过 | 所有相关阶段通过；16文件逐项匹配，解压 verifier exit 0 | 未执行 |
| ARM64 | 未尝试：MSVC ARM64 cl/link 与 VC 标准库缺失 | 未生成包；没有降级冒充 | 未执行 |

x64 共 15 个具名工具阶段全部 exit 0，另有 .NET ZIP 数据解压及逐文件比较通过。

## 固定源码和工具身份

- 源码目录：`<SOURCE_ROOT>`
- 候选索引 tree：`98cb4681032285309841293d65f0041344aa5f14`
- HEAD：`9e4e9c1fdeacc0eae8318a1bf40a04e8c1da97e7`
- 固定 main / MERGE_HEAD：`0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`
- 源码清单 SHA256：`41cf83f1cf060df6842fc55f9cc5676668944057ac825519efe20b0bb5e9aee5`
- 末轮索引比较和未暂存 diff 均为0，无未合并/未跟踪文件。仍已暂存、未提交；未修改源码，未追更新 main，未刷新源码清单。
- 宿主 <HOST> / AMD64 Windows；PowerShell 7.6.5，已有 Node 24.19.0，CMake 3.31.6-msvc6，MSBuild 17.14.60.43110，MSVC 19.44.35229.0 / Tools 14.44.35207，Windows SDK 10.0.19041.0。
- x64：Visual Studio 17 2022，`-A x64 -T v143,host=x64`，Release。实际 Compiler.cmake 记录 Hostx64/x64/cl.exe、architecture x64、pointer size 8。
- x64 cl SHA256：`fe251ef50a1545b1b0835ee17b1e785459712b38d79e45b5c1d3d28970a36619`；link SHA256：`a364af801a8539e4324d9489313dbf001d959128451fc99a27b24676bbac058f`。

Win32新输出根为 windows-win32-package，输入取已完成的 windows-win32-desktop 模板 EXE 和安装许可；没有重跑已通过的 Win32 构建。x64所有新输出在 windows-x64，固定官方 WebView2包只读复用 windows-win32-desktop/deps 中已核对的1.0.4258.31：包SHA256 `56f7f4b8bf9aee4b8efefbbdd4f67d5f74ebd1b100ed0806da71bf76af481aa9`。没有下载或安装新编译器、浏览器Runtime、全局组件或包源。

## 构建与文件处理边界

每个架构开始前审查可达链及实际生成项目。固定根CMake的 Portable/Desktop 分支、Tests/Example/trace OFF；正常架构/ABI/atomic探测只编译链接，没有 try_run 或探测产物执行。实际项目没有应用 PreBuild/PreLink/PostBuild；只有官方CMake stamp检查。SDK安装只文件处理。

x64 配置使用 `-A x64`。首次 project 前清理进程级 CMake/toolchain/launcher/flags/import/Node preload；UserRootDir、MSBuildUserExtensionsPath、VCLibPackagePath 和 VcpkgRoot 指向独立空目录，关闭 vcpkg/manifest/artifacts 与 Directory.Build/user wildcard 导入。环境、CMake globals、MSBuild /p 三层隔离；构建为 `--parallel 1 /m:1 /nodeReuse:false /noAutoResponse`。

正常模板使用安装版生成器及已审阅的自有资产同步。仅在新目录内复制、hash、写ownership清单、rename及清理自身文件；没有执行模板程序。stage/verify/archive四工具依赖闭包只有Node内置模块和相邻审阅文件：stage生成doctor文本但不运行，verify只读文件/PE，archive写stored ZIP32并核header/CRC/SHA，不调用外部压缩程序或应用。

解压只使用现有 .NET ZipFile，预先检查目录边界、重复/大小写冲突和链接条目，解压到新的 expanded 目录；不执行包内任何内容。随后比较原stage与展开目录的完整文件集合、大小和SHA，并运行相同的只读verifier。

## x64 实际阶段

下表保留已执行阶段的精准目标、核心操作和工具退出码；原始机器命令与日志未随本公开归档复制。

| 阶段 | 精准目标 / 核心操作 | exit |
|---|---|---:|
| desktop-configure | 新db，Desktop/Portable ON，Tests/Example/trace OFF，固定官方包和x64工具 | 0 |
| desktop-build | soui_desktop，含正常Runtime库依赖，单作业 | 0 |
| desktop-install | 新 di 前缀，清DESTDIR | 0 |
| runtime-consumer-configure | 新 rcb，仅 di/SouiRuntime | 0 |
| runtime-consumer-build | runtime_install_headers_linkcheck | 0 |
| runtime-allheaders-build | installed_runtime_header_compileall OBJECT | 0 |
| consumer-configure | 新 cb，仅 di/SouiDesktop及Runtime | 0 |
| consumer-build | desktop_installed_linkcheck | 0 |
| template-generate | 安装版工具，新template-src，原模板，应用0.1.0 | 0 |
| template-configure | 新tb，仅di前缀，APP trace OFF | 0 |
| template-build | 正常InstalledDesktopCheck含自有文件资产同步 | 0 |
| stage | 新 staged，windows/x64，v2应用目录 | 0 |
| verify-staged | 只读完整性与架构验证 | 0 |
| archive | 新delivery，ZIP32 stored与CRC/SHA读回核对 | 0 |
| verify-expanded | 解压后再次只读验证 | 0 |

Win32 仅新增 stage、verify-staged、archive、verify-expanded 四个 Node 文件工具阶段，全部 exit 0。原有编译警告和 CMake 兼容变量警告保留，没有修改源码或放宽断言。

## 安装、编译和链接来源

x64安装共62文件，22个公共头全部与固定候选对应源码逐字节匹配；两库与生产输出匹配，x64 loader与固定官方包匹配，四份完整许可与来源匹配。所有安装目标均在新di前缀。

| x64安装库 | bytes | SHA256 |
|---|---:|---|
| soui_runtime.lib | 11,286,178 | `f72f5dbe2a48221b171df25db87500121ce5c6acede281fa90fbc440cb6187a1` |
| soui_desktop.lib | 4,782,682 | `3f2f044e2eaa34bd6a44dae9f11bc100c294a3a2be89da3fc22e82c7734c6a30` |
| WebView2LoaderStatic.lib | 11,463,528 | `e66284970876a616c82e7dcda75a17fd46d04827f9de81d4299b8862d34d25d6` |

实际编译器依赖：Runtime独立消费者129项，项目头11；全部安装头聚合编译165项，项目头21；Desktop消费者143项，项目头7。项目头全部来自新di/include，无旧SDK或源码回退。Template CL.read确认app.h来自di/include，模板自身头来自生成目录。

Runtime-only、Desktop consumer、正常模板的实际link.read均只从本架构di读取相应SDK库；Runtime-only读取Runtime archive，其余二者读取Desktop、Runtime、官方loader三个archive。没有旧windows-win32-runtime/windows-win32-desktop、依赖目录或producer库回退。

23个安装工具/模板/frontend文件匹配候选来源；17个生成模板文件符合三项token替换及原始adapter复制；8个同步web资产匹配生成源码和ownership清单。证据见 install-identity、compiler-dependency-identity、template-source-and-assets-identity、actual-link-and-template-identity JSON及原始tlog。

四份许可在两种架构stage、ZIP和展开目录中均保留原字节：

| 交付路径 | 来源 | SHA256 |
|---|---|---|
| legal/LICENSE.md | SDK LICENSE.md | `e66ea76381329e7ac8907b5022c1c4b3e0fb3f5a1a089653f86f3d0312059ac6` |
| legal/THIRD_PARTY_NOTICES.md | Portable Runtime notices | `547c0c03fb3a333060dff8d512a3f139805c548ca513c92e7e20b717cd35657f` |
| legal/WebView2-LICENSE.txt | 固定官方NuGet LICENSE | `0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55` |
| legal/WebView2-NOTICE.txt | 固定官方NuGet NOTICE | `106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90` |

## 按架构交付产物

两包均为 `soui.desktop.application-stage/2` 输入和 `soui.desktop.application-delivery/1` 交付记录，ZIP内20 entries，16个文件（stage的15个payload加application-manifest.json）。原stage、ZIP成员、展开目录三方文件集合及SHA相同。两个解压后的verifier均 `integrity_consistent`、noticeSetVerified=true，checkedFiles=15。

| 架构 | ZIP路径（任务根相对） | bytes | SHA256 |
|---|---|---:|---|
| x86 | windows-win32-package/delivery/InstalledDesktopCheck-0.1.0-windows-x86.zip | 530,377 | `7a0405f884b89849aec8e65928d1dcbca6db2a1c0d537c5d270fcd08fcd45ee0` |
| x64 | windows-x64/delivery/InstalledDesktopCheck-0.1.0-windows-x64.zip | 605,641 | `ee11525199017d5d91946e0eb96150b4a8dd3b5d2df80830057c4a2f4a739062` |

应用manifest原始SHA256：x86 `1d4f750a39a263b545e467b2cebaf4f025bbcf2d341f6237a7c1eef5ab8ca6ab`；x64 `eacf31feb8a86239589017952e449cf164bdcf3aff97e050527dba7a666df2ec`。ZIP哈希同时匹配delivery manifest、`.sha256` sidecar及独立重算。

x64三份EXE仅解析字节，均AMD64 0x8664 / PE32+ 0x020b / console3：

| 产物 | bytes | SHA256 |
|---|---:|---|
| Runtime consumer | 41,984 | `08f1698c51b09d01df78dc6f62390e9af75860329e9a37e12dd9b171f8693311` |
| Desktop consumer | 287,232 | `90a083c9d4f2912592d6e2ed66aada40eb31a39fa03045bd7b713fdef4294a53` |
| 正常模板 | 562,688 | `f0efa9b5c67815f5b28018a6dc60f2d42996e4af87744d2464c7eaa1bafc9bfb` |

Win32沿用的模板为I386/PE32，487,424bytes，SHA256 `8a3e4d0c3e1be6f6c95949f2d9d5489e4800e5f3322c549f6b03259791acd42b`，没有重建或启动。

## ARM64 的具体缺口

已安装MSVC版本目录只有14.44.35207。Hostx64/arm64、Hostx86/arm64目标编译器目录及lib/arm64均不存在；官方vswhere查询 `Microsoft.VisualStudio.Component.VC.Tools.ARM64` 返回 `[]`，查询exit 0。x64编译器及VC库完整存在。Windows SDK的ARM64 kernel32/ucrt/libucrt以及固定WebView2包的ARM64组件不能代替缺少的MSVC目标工具/VC库。

因此ARM64 configure/build/install/link/package均未尝试，不是构建失败或权限拒绝；未安装新编译器、未改用x64并冒充ARM64。详细路径及hash在 tool-availability.json。后续需要具备相应官方目标工具的合法环境，才可补这一独立架构证据。

## 资格边界及保留证据

`releaseReady=false`、`nativeRuntimeVerified=false`保持不变。需区分：stage/2和delivery/1原schema没有releaseReady字段，未为了报告而向其插入未知字段；源码及本地资格汇总保持releaseReady=false。stage明确 developmentOnly=true、signed=false、dependenciesBundled=false、nativeRuntimeVerified=false；delivery的integrityVerified=true只表示文件一致，authenticityVerified/nativeRuntimeVerified/signed仍false。

doctor.ps1只是生成、复制并hash，没有运行；没有启动EXE、CTest、GUI、doctor或launch，也没有应用PID、输出或退出码。全部exit 0属于编译器/链接器/文件工具，不是应用执行成功。没有签名、Runtime安装或生产发布结论。

旧 primitives 的 4551 技术错误和旧 consumer 底层原因未知记录保留；相关 EXE 及 7 项历史证据散列未变。primitives 未构建或启动。本轮 x64 仅完成编译和文件交付，不能作为旧目标的运行结果。

此历史阶段完成 x86/x64 compile/link/package，没有应用运行证据；ARM64 工具缺口仍未关闭。历史结果不验证当前恢复候选。
