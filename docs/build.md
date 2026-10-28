# 构建与源码范围

## 环境

- Windows x64 开发机、Visual Studio 2022 C++ 桌面开发组件、ATL、v143 工具集。
- MSVC **14.44.35207** 和 Windows SDK **10.0.19041.0**；Miniblink 脚本固定使用这两个版本。
- CMake ≥ 3.24、npm、PowerShell 5.1 或以上。基础 SDK/兼容生成项目支持原有 Node.js 20.19 LTS 或 ≥ 22.12；0.4 整体构建包含现代模板，请使用 Node.js ≥ 22.12。
- 完整重编译内核建议保留至少 40 GB 空间，双架构需要更多。运行 `tools/Doctor.ps1` 检查环境。

公共 CMake 配置入口同样固定 v143,host=x64,version=14.44.35207 和 SDK 19041。Doctor.ps1 检查同一 MSVC 目录的 Hostx64 x86/x64 编译、链接工具及 ATL；缺失时不会回退到其他小版本。旧缓存未记录该工具集或记录不符时，使用新的输出目录，或显式 -FreshConfigure 刷新已确认属于当前工程的缓存。此选择不代替实际编译器路径、CRT 或目标系统运行验收。

旧系统交付固定工具链和运行库身份，不使用会随时间变化的最新 Runtime 下载链接自动替换载荷。微软已说明 VS 2026 起不再以 Win7/8/8.1 为目标，最新 v14 Redistributable 只支持 Win10/11 和所列 Server；依据分别见 [C++ 目标系统表](https://learn.microsoft.com/en-us/cpp/overview/supported-platforms-visual-cpp?view=msvc-170)和 [Runtime 分发说明](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)。

2026-10-09 已实际读取随包 x86/x64 的 `msvcp140.dll`、`vcruntime140.dll`，两者文件版本均为 `14.44.35211.0`；两架构 `ucrtbase.dll` 为 `10.0.19041.5609`，对应散列仍由依赖清单核验。文件版本符合所选工具链不等于已证明 Win7、Win8 或 PE 的导入、装载与业务兼容；这些行保持待验收。运行库替换须刷新载荷身份、分发许可和受影响系统的实测结果。

## 日常构建

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -Install
powershell.exe -File tools/Build.ps1 -Architecture x86 -Configuration Debug
powershell.exe -File tools/Build.ps1 -SdkOnly -Architecture x64 -Install
```

日志在 `out/<架构>/`，SDK 单独构建日志在 `out/sdk-<架构>/`。`-SkipWeb` 仅供已有最新 Web 产物时使用。默认构建会刷新运行目录中的 Web 文件和 `resources/legal/` 声明材料，即使 C++ 无需重新链接。运行时复制整个应用输出目录；声明材料属于载荷身份和体积统计。它们仍须与实际链接成员和再分发要求逐项核对，不能代替许可审查。

应用和 SDK 的 Debug 配置保留调试信息，但为匹配固定 SOUI 运行库仍使用 `/MD` 和相同的发布版运行库边界，且定义 `NDEBUG`。它不代表底层依赖也以 Debug CRT 编译；不要混入其他配置的 SOUI DLL/LIB。

安装后的 SDK 位于 `out/install/<架构>/`，第三方声明在 `share/SouiSdk/`；外部项目通过 `find_package(SouiSdk CONFIG REQUIRED)` 和 `Soui::Sdk` 引用。此导出包支持 SDK 核心链接；完整应用的资源与运行库部署使用源码包里的 `cmake/SouiApplication.cmake`。

## 底层源码重编译

```powershell
powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x64 -Component All
powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x86 -Component All
```

可用 `-Component Miniblink` 或 `SOUI` 单独构建。Miniblink 默认从随包源码构建依赖库、V8、软件 Skia 和软件内核；显式传入 `-EnableExperimentalGpu` 才额外构建、链接实验 Skia GPU，它仍需单独验收。SOUI 构建 utilities、soui、render-gdi、imgdecoder-gdip、soui-sys-resource 五个组件。

`-OutputRoot` 可指定新的输出目录，保留历史构建产物。All/SOUI 要求该目录位于本 SDK 的 `out/` 内，单独 Miniblink 可选外部目录；默认路径保持下表所列位置。内核资源输出位于中间目录，x86 工具链使用 Win32 工程平台。

Miniblink 主内核含数千个编译单元，完整构建可能持续较长时间；默认脚本限制编译并行度以控制内存。详细进度写入对应组件日志，长时间没有终端输出不等于停止。首次成功后，保留 out 目录可复用增量结果。

| 输出 | 路径 |
|---|---|
| 新 x64 内核 | `out/dependencies/x64/miniblink/bin/miniblink.dll` |
| 新 x86 内核 | `out/dependencies/x86/miniblink/bin/node.dll` |
| 依赖与日志 | `out/dependencies/<架构>/` |
| SOUI DLL / LIB | `out/dependencies/<架构>/soui-source-<构建 ID>/bin/` 或 `bin64/` |
| 构建记录 | `out/dependencies/<架构>/build-All.json` |

源码构建不会覆盖 `third_party/prebuilt/`。SOUI 的旧工程有写入自身源码目录的硬编码规则，构建入口先将 Git 管理或完整源码清单列出的文件复制到新的 `out/dependencies/<架构>/soui-source-*` 目录；其构建产物和实际路径写入报告。验证新 DLL 时先构建应用，将应用输出目录复制到隔离目录，用新内核替换 `node.dll`、同架构新 SOUI 组件替换对应 DLL，再执行模板 `--smoke-test` 与 Demo `--webview-smoke-test --arch=x64 --react`。短测通过后仍需长期内存、关闭稳定性和兼容性验收。

以上隔离替换与启动验证已封装为 `powershell.exe -File tools/Test-Dependencies.ps1 -Architecture x64`，结果在 `out/dependencies/x64/rebuilt-smoke.json`。

本包提供第三方来源记录和本地修改补丁，不承诺不同 MSVC 小版本的二进制逐字节相同。Microsoft 编译器、Windows SDK、VC/UCRT 和系统 DLL 使用官方安装与授权，不包含其源码。首次 npm 安装需要网络或缓存；归档不包含 node_modules。

### 显式 LLVM 组件重建

`LLVM22MSVC142` 固定 LLVM 22.1.3、MSVC/ATL 14.29.30133、SDK 19041 和
AMD64 构建工具，不改变默认 MSVC143 入口。内核使用 `Build-Dependencies.ps1`
的 `-Component Miniblink`；SOUI 使用 `Build-SouiLlvm.ps1`。两者必须显式
传入 `-Architecture`、`-NativeToolchain LLVM22MSVC142`、新的 `-OutputRoot`
以及 `-LlvmRoot`、`-MsvcRoot`、`-WindowsSdkRoot`、`-NinjaExe`、
`-WindowsCmdExe`、`-PowerShellExe`、`-VsInstallDir` 的实际绝对路径。
SOUI 还要求 `-NodeExe`，输出必须是当前 SDK `out/` 内尚不存在的目录。

入口保留 `build-Miniblink.json` 或 `build-SOUI.json`、实际参数、日志、
源码快照、工具身份及产物散列；SOUI 使用隔离源码副本。构建调用还须由
OwnedProcessRunner 捕获完整进程组的自然退出。`standard-build.cjs` 的
`capture` 帮助函数保存 `<名称>.ndjson`、context、ready、finished 和原始
标准输出/错误；组件文件报告本身不宣称整个进程组或工具链已经合格。

从已验明的报告导入两架构 DLL/LIB、刷新依赖身份并重新构建 SDK 后，执行
`Test-Dependencies.ps1 -NativeToolchain LLVM22MSVC142`。该命令另外要求
`-KernelBuildReport`、`-SouiBuildReport` 两份实际报告，
`-KernelOwnedCapture`、`-SouiOwnedCapture` 两个捕获文件前缀（不带
`.ndjson`），`-BuildRunner` 的实际监督器路径，以及 `-ProfileRequest`
指向当前架构 Release 构建请求；`-DevkitRoot` 默认当前 SDK。
所有输入均使用绝对路径，架构必须一致。

本次 0.5 导入的内核来源为 `third_party/manifests/kernel-build-050.json`。
刷新依赖清单时显式执行
`node tools/internal/Update-DependencyManifest.cjs --kernel-provenance third_party/manifests/kernel-build-050.json`；
无参数模式只适用于工具中固定的历史导入，不会自动认可新的 DLL。

此入口先检查当前已接受的应用载荷确实携带这六个重建 DLL，再执行现有
模板、Workbench 默认和 React 三个 45 秒场景。结果写入
`out/dependencies/<架构>/rebuilt-smoke-llvm.json`。完整收集使用
`node tools/Collect-Verification.cjs --native-toolchain LLVM22MSVC142`，
重新验证组件与应用的原始证据，不要求伪造合并的 `build-All.json`。

## SDK 测试与私有内核回归

根目录默认保持 `SOUI_BUILD_TESTS=ON`。完整开发源码包含 Miniblink 私有目录时，默认同时注册厂商回归；可搬移的 SDK 源码工作区完全不包含该目录时，默认仅关闭 `SOUI_BUILD_VENDOR_TESTS`，SDK 自身测试仍构建。私有目录存在但缺少任一必需文件会使配置失败，不能把损坏的输入视为精简交付。SDK Core、生成项目和完整开发源码的范围不同，私有测试关闭不表示内核已验收。

需要厂商回归时，从包含完整输入的开发源码根显式执行 `powershell.exe -File tools/Build.ps1 -Architecture x64 -WithVendorTests -FreshConfigure`；x86 使用对应架构。该开关向根 CMake 同时传入 `SOUI_BUILD_TESTS=ON` 和 `SOUI_BUILD_VENDOR_TESTS=ON`，不允许与 `-SdkOnly` 共用，也不接受生成应用目录。构建完成后仍须实际运行 `Test.ps1 -RequireVendorTests`，不能用成功配置或注册清单代替测试结果。

## 可选 Windows 辅助功能模块

`tools/Build.ps1 -Architecture x64 -Configuration Release -WithUia` 或 `tools/Soui.ps1 build --architecture x64 --configuration Release --with-uia` 显式构建 `Soui::Uia`；可组合既有 `-SdkOnly`、`-Install` 和现代后端选项。未选择时明确配置 OFF。应用仍需按公共 API 注册可信原生投影和派发策略，构建选项不会自动投影业务页面。

CMake 消费者使用 `find_package(SouiSdk 0.5.1 EXACT CONFIG REQUIRED COMPONENTS Uia)` 与 `Soui::Uia`。未安装该组件会拒绝 REQUIRED 请求。历史 MSVC 编译器尾进程例外未覆盖 UIA，不能与该例外组合；正常 MSVC/LLVM 构建保持各自既有验证规则。
