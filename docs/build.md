# 构建与源码范围

## 环境

- Windows x64 开发机、Visual Studio 2022 C++ 桌面开发组件、ATL、v143 工具集。
- Windows SDK **10.0.19041.0**；脚本固定使用此版本。
- CMake ≥ 3.24、Node.js ≥ 20、npm、PowerShell 5.1 或以上。
- 完整重编译内核建议保留至少 40 GB 空间，双架构需要更多。运行 `tools/Doctor.ps1` 检查环境。

## 日常构建

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -Install
powershell.exe -File tools/Build.ps1 -Architecture x86 -Configuration Debug
powershell.exe -File tools/Build.ps1 -SdkOnly -Architecture x64 -Install
```

日志在 `out/<架构>/`，SDK 单独构建日志在 `out/sdk-<架构>/`。`-SkipWeb` 仅供已有最新 Web 产物时使用。默认构建会刷新运行目录中的 Web 文件，即使 C++ 无需重新链接。运行时复制整个应用输出目录。

应用和 SDK 的 Debug 配置保留调试信息，但为匹配固定 SOUI 运行库仍使用 `/MD` 和相同的发布版运行库边界，且定义 `NDEBUG`。它不代表底层依赖也以 Debug CRT 编译；不要混入其他配置的 SOUI DLL/LIB。

安装后的 SDK 位于 `out/install/<架构>/`，外部项目通过 `find_package(SouiSdk CONFIG REQUIRED)` 和 `Soui::Sdk` 引用。此导出包支持 SDK 核心链接；完整应用的资源与运行库部署使用源码包里的 `cmake/SouiApplication.cmake`。

## 底层源码重编译

```powershell
powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x64 -Component All
powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x86 -Component All
```

可用 `-Component Miniblink` 或 `SOUI` 单独构建。Miniblink 的依赖库、V8、Skia GPU 和内核全部由随包源码构建。SOUI 构建 utilities、soui、render-gdi、imgdecoder-gdip、soui-sys-resource 五个组件。

Miniblink 主内核含数千个编译单元，完整构建可能持续较长时间；默认脚本限制编译并行度以控制内存。详细进度写入对应组件日志，长时间没有终端输出不等于停止。首次成功后，保留 out 目录可复用增量结果。

| 输出 | 路径 |
|---|---|
| 新 x64 内核 | `out/dependencies/x64/miniblink/bin/miniblink.dll` |
| 新 x86 内核 | `out/dependencies/x86/miniblink/bin/node.dll` |
| 依赖与日志 | `out/dependencies/<架构>/` |
| SOUI DLL / LIB | `third_party/sources/soui/bin/` 或 `bin64/` |
| 构建记录 | `out/dependencies/<架构>/build-All.json` |

源码构建不会覆盖 `third_party/prebuilt/`。验证新 DLL 时先构建应用，将应用输出目录复制到隔离目录，用新内核替换 `node.dll`、同架构新 SOUI 组件替换对应 DLL，再执行模板 `--smoke-test` 与 Demo `--webview-smoke-test --arch=x64 --react`。短测通过后仍需长期内存、关闭稳定性和兼容性验收。

以上隔离替换与启动验证已封装为 `powershell.exe -File tools/Test-Dependencies.ps1 -Architecture x64`，结果在 `out/dependencies/x64/rebuilt-smoke.json`。

本包提供第三方来源记录和本地修改补丁，不承诺不同 MSVC 小版本的二进制逐字节相同。Microsoft 编译器、Windows SDK、VC/UCRT 和系统 DLL 使用官方安装与授权，不包含其源码。首次 npm 安装需要网络或缓存；归档不包含 node_modules。
