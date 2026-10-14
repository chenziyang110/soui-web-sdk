# SOUI SDK 源码交付

**开发版本：0.4.0-preview.3（依赖整合，尚未发布）。** 面向 Windows 的 C++ / TypeScript 桌面 SDK，提供独立 SDK、最小模板、完整 Demo、第三方源码和构建工具。这个目录是可独立搬移的交付根目录，构建不依赖原研发仓库或开发者机器上的源码路径。

最近已发布版本为 0.4.0-preview.1，见 [发布说明](docs/releases/0.4.0-preview.1.md)。整合版本包含公平调度内核、会话续期、菜单/托盘/快捷键、安装恢复与 Agent 动作；历史发布标签和固定 SDK 包保持原内容。当前 0.4 工作增加可选 WebView2、现代模板、环境与前端能力探测，见 [接入说明](docs/modern-backend.md) 和 [执行清单](docs/roadmap/0.4.0.md)。历史验收不能转述为当前开发版本已通过；完整性能、Win7/PE 支持、独立 Agent 身份及生产门槛仍待实际验证。

长期维护、AI Agent、评审、测试和发布的统一规范见 [AGENTS.md](AGENTS.md)；所有贡献者和自动化工具在修改前都应阅读。

```text
soui-sdk/
├─ sdk/                    SDK：公共头文件、C++ 源码、Web SDK、CMake 导出
├─ templates/desktop-app/   新项目模板：React 页面 + C++ 调用 + 标准窗口
├─ demos/workbench/         完整 Demo：媒体、文件扫描、组件和桌面能力
├─ third_party/
│  ├─ include/             开发头文件
│  ├─ prebuilt/x86|x64/     固定版本的运行库和导入库
│  ├─ sources/             Miniblink、SOUI、zlib 的源码
│  ├─ licenses/            保留的第三方授权文本
│  └─ manifests/           依赖身份及核验信息
├─ cmake/                  应用接入和运行文件部署
├─ tools/                  检查环境、构建、新建项目、测试、打包
├─ tests/                  SDK 协议、安装器、Web 和外部接入测试
├─ docs/                   架构、API、构建、升级、发布及故障排查
├─ verification/           本次交付的验收结果
├─ CMakeLists.txt
└─ VERSION
```

## 首次编译

准备 Windows x64 开发机、Visual Studio 2022 的 C++ 桌面开发组件和 ATL、Windows SDK 10.0.19041.0、CMake 3.24 或以上。0.4 整体构建包含现代模板，使用 Node.js 22.12 以上；基础 SDK 和兼容生成项目保持原有 Node.js 20.19 支持范围。当前实际验证的工具版本见 `verification/acceptance.json`。

在本目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Doctor.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Architecture x64 -Configuration Release -Install
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test.ps1 -Architecture x64 -Configuration Release
```

构建会安装锁文件约束的 Web 构建依赖、编译 SDK/模板/Demo，并把运行依赖部署到输出目录。首次安装 npm 依赖需要网络或预先配置的 npm 缓存；运行交付的桌面程序不需要 Node.js 或网络。

| 产物 | 默认位置 |
|---|---|
| 模板应用 | `out/x64/bin/Release/SouiStarter/SouiStarter.exe` |
| 完整 Demo | `out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe` |
| 可供外部工程引用的 C++ SDK | `out/install/x64/` |
| Web SDK JavaScript / 类型定义 | `sdk/web/dist/` |

仅编译 SDK：`powershell.exe -File tools/Build.ps1 -SdkOnly -Architecture x64 -Install`。32 位把 `x64` 换成 `x86`；调试配置使用 `-Configuration Debug`。

## 从模板开始新项目

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/New-Project.ps1 -Name MyDesktopApp -OutputRoot C:\dev\MyDesktopApp
cd C:\dev\MyDesktopApp
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 -Architecture x64
```

新项目包含业务源码和固定版本的 SDK 源码，脱离本交付目录也能继续编译。业务开发修改 `app/`、`web/`、`contracts/` 和 `resources/`；SDK 文件由 `sdk-lock.json` 管理。

## 重编译底层依赖

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build-Dependencies.ps1 -Architecture x64 -Component All
```

这个入口从随包源码编译 Miniblink 的静态依赖、V8、Skia GPU、内核以及 SOUI 的五个运行组件。它与日常 SDK / 业务构建分开，耗时和空间需求更大。详细产物、复验和已知限制见 [源码构建说明](docs/build.md)。Microsoft 编译器、Windows SDK 和系统运行库仍使用开发机安装的官方工具；它们不是本项目可交付的源码。

## 文档导航

- [接入与开发](docs/getting-started.md)
- [架构与目录所有权](docs/architecture.md)
- [私有存储请求的独立读端解释（ADR-0082）](docs/adr/0082-task-store-private-storage-readers.md)（Source 与纯 JS；真实长期采集和完整缓存预算仍待验证）
- [TaskStore 空存储清理的退款记账（ADR-0081）](docs/adr/0081-task-store-empty-deallocation-accounting.md)（独立 x86 fault 回归已通过；完整 SDK 与 x64 回归仍待验证）
- [C++ / Web API](docs/api.md)
- [完整构建与底层源码](docs/build.md)
- [版本、升级与兼容性](docs/versioning.md)
- [测试与验收](docs/testing.md)
- [发布与打包](docs/releasing.md)
- [故障排查](docs/troubleshooting.md)
- [已知限制与生产门槛](docs/known-issues.md)
- [0.3 迭代计划](docs/roadmap/0.3.0.md)
- [0.4 执行清单](docs/roadmap/0.4.0.md)
- [现代后端与运行档位](docs/modern-backend.md)
- [模块化 GUI 与 Agent SDK 建设路线](docs/roadmap/agent-gui-framework.md)
- [WebView2 独立实验消费者](experiments/webview2-consumer/README.md)
- [0.3 基础切片进展与验证](docs/roadmap/0.3-foundation.md)
- [0.3 SDK 生产能力与价值验证方案](docs/roadmap/0.3-sdk-validation.md)
- [生产验证执行记录与原始证据](docs/acceptance/progress.md)

当前是预览源码交付。独立构建与接入验证通过，不等于生产发布验收完成；x64 长时间内存与关闭稳定性、目标系统矩阵等剩余项目在已知限制中逐项记录。
