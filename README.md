<p align="center">
  <img src="docs/assets/readme-hero.svg" alt="SOUI Web SDK — Web 的表达力，C++ 的掌控力。" width="100%">
</p>

<h1 align="center">SOUI Web SDK 0.6.0</h1>

<p align="center">
  <strong>用 Web 构建界面，用 C++ 掌握桌面业务、权限与生命周期。</strong><br>
  Windows x86 / x64 · C++17 / TypeScript · 显式后端选择 · 原生受控 DevTools
</p>

<p align="center">
  <a href="docs/releases/0.6.0.md">0.6.0 发布说明</a> ·
  <a href="https://github.com/chenziyang110/soui-web-sdk/releases/tag/v0.6.0">发布与下载入口</a> ·
  <a href="#quick-start">开始开发</a> ·
  <a href="docs/devtools.md">DevTools</a> ·
  <a href="docs/known-issues.md">已知限制</a>
</p>

SOUI Web SDK 把业务页面、类型化桥接、原生宿主、运行依赖与交付工具放在一套可独立构建的源码工程中。前端编写界面，C++ 注册业务动作和任务，应用决定允许谁调用、何时取消，以及如何结束当前窗口与 Runtime。

> 本地集成候选（未发布）：固定 PR 9e4e9c1f 与固定 main 0cdc0a9a 的合并采用 main 的 VERSION=0.6.0，原 PR 身份为 0.6.0-preview.3。当前候选未编译、运行或发布；下列发布与历史验证仅对各自原始身份有效。本地内容不等同已发布的 0.6.0 资产，发布前须协调新内容的版本身份。

**0.6.0 采用普通 GitHub Release 分类；完整资格保持 `releaseReady=false`、`productionQualificationComplete=false`、`fullDevtoolsGoalComplete=false`。** 两架构内核与必要 SDK 构建、安装消费者、默认应用八次启动/自然关闭及九份供应/应用 ZIP 已有实际记录。双后端、双架构的受控 DevTools 代表例及 MiniBlink x86 原始 TSX 断点/单步观察已完成，具体范围见[发布说明](docs/releases/0.6.0.md)；普通 Release 分类不代表生产、目标系统或性能资格。

## 获取 0.6.0

[v0.6.0 发布页](https://github.com/chenziyang110/soui-web-sdk/releases/tag/v0.6.0)是附件、完整对应源码与 SHA-256 的固定入口；最终发布身份以其清单为准。

| 用途 | 资产 |
|---|---|
| 创建独立项目 | `SouiSdk-0.6.0-source-sdk.zip`，逐文件锁定的 source SDK |
| 运行示例 | `SouiStarter`、`SouiModernStarter`、`SouiWorkbench` 的 `-0.6.0-windows-x64.zip` / `-x86.zip`，六包及侧车已核完整内容 |
| MiniBlink 离线调试材料 | `SouiDevTools-0.6.0-windows-x64.zip` / `-x86.zip`，仍需应用显式配置和原生授权 |
| 完整对应源码 | `soui-sdk-0.6.0-full-source.zip`，按当前 Git 源清单与显式 UTF-8 文件名封存 |

source SDK 与两份 DevTools 组件包已核逐文件闭集。两架构默认应用的八次运行在 Windows `10.0.20348.587` 完成；Modern 使用 WebView2 Runtime `156.0.4314.50`。这组短测不覆盖全部目标系统、AutoHost 双启动路径或长期稳定性。最终资产名、散列和实际范围以[发布说明](docs/releases/0.6.0.md)及[机器事实记录](verification/production-validation/0.6.0-release-2026-10-29/RELEASE-FACTS.json)为准，下载以发布页实际附件为准。

## 未发布本地集成：跨平台 Runtime 与统一桌面

以下跨平台功能及限定记录来自固定 PR 9e4e9c1f（版本 0.6.0-preview.3），尚未在本地集成候选重新编译或运行。

固定 PR 新增显式 [跨平台 Runtime](docs/cross-platform.md)：无窗口的任务、权限、
Bridge 协议和文件扫描可独立构建为 `Soui::Runtime`，覆盖 Windows/Linux
x86/x64/ARM32/ARM64 与 macOS x64/ARM64/universal2 的目标策略。
另新增 [统一桌面入口](docs/unified-desktop.md) `Soui::Desktop`：同一份 C++ 与 Web
源码接入 Linux WebKitGTK、macOS WKWebView、Windows WebView2，包含窗口生命周期、
独立会话的多窗口、双向 Bridge、层级原生菜单和有明确失败的托盘能力。
新增 `tools/package-desktop.cjs` 统一构建、测试、安装与 SDK 开发快照打包入口，
不是应用安装器。窗口状态/关闭保护、独立授权系统服务、层级菜单、宿主全局快捷键及
打开/保存路径选择已接入统一接口。Linux 有历史限定真实 GUI/Bridge 证据，最新功能尚未
重新进行原生交互验收；macOS universal2 构建交付及 Apple Silicon 主机限定窗口/Bridge/模块/fetch、DOM 关闭否决探针已过。
Windows x64 Release 的安装 SDK、独立消费者交付及真实 WebView2 Bridge/模块/fetch/公开关闭探针已过；
2026-10-26 原模板补充可见窗口、app.info、模块/同源 fetch、原生标题栏最大化/还原及正常窗口关闭验证。原模板进程退出码未测，其他架构及完整原生能力仍待验收。当前状态见[集中验收矩阵](docs/acceptance/unified-desktop-2026-10-23.md)。
现有下载包不含这些未发布功能，不能据此宣称 Electron 全功能兼容。

<a id="capabilities"></a>

## 0.6 带来了什么

| 方向 | 可以接入的能力 | 应用需要明确的边界 |
|---|---|---|
| 原生调试 | 公共 Host 的配置、不透明 grant、异步打开/撤销及状态观察；兼容与现代模板、Workbench 的原生入口 | 默认 disabled；每次许可由可信原生处理器发放；结束诊断可关闭当前业务目标 |
| 后端选择 | compatibility、modern，以及启动前选择后端的可选 AutoHost | 业务启动后的错误不会自动换后端或重放操作；WebView2 Runtime 不随应用自动下载 |
| Web 构建与资源 | 显式构建目标、资源清单、发行档与离线源码诊断档 | 发行档默认不带业务 source map；源码诊断可能暴露源码与数据 |
| 输入与呈现 | 有界输入接纳/恢复、调度、帧和资源通知、可选数值诊断的源码接线 | 具体能力取决于匹配内核；源码机制不构成帧率、尾延迟或 GPU 性能保证 |
| 交付与恢复 | 固定 SDK 锁、运行时组件清单、失败恢复、可选去重 Bundle 与压缩档位 | 原 ZIP 仍可使用；Bundle 须先恢复完整应用目录；应用数据不能放入受管资源目录 |

公共 SDK 与 Demo 分层维护。Workbench 的文件扫描、媒体界面等业务适配器属于演示层；它们不自动成为公共 SDK 的 API 或权限。

## 选择运行路径

| | compatibility | modern | auto |
|---|---|---|---|
| 宿主 | SOUI + MiniBlink | `Soui::WebView2` | 可选 `Soui::AutoHost` |
| 模板 | `desktop-app` | `modern-app` | `auto-app` |
| 页面 | Chrome 49 兼容构建 | 现代 Web 构建 | 分别构建现代与兼容页面 |
| Runtime | 随应用部署固定 DLL 与资源 | 系统或调用方准备 WebView2 Runtime | 可用时选 WebView2，明确不可用时选随包 MiniBlink |
| 创建参数 | 默认或 `-Profile compatibility` | `-Profile modern` | `-Profile auto` |

AutoHost 只在启动前决定后端。配置、权限、页面和异步准备失败仍是失败，不能借“降级”掩盖。它不安装 Runtime，也不在页面已经挂载后切换后端。完整规则见[自动后端](docs/auto-backend.md)与[现代后端](docs/modern-backend.md)。

最终用户运行已打包应用无需安装 Node.js。SDK 不捆绑 Fixed WebView2 Runtime 或 Windows 字体文件。具体 Windows、PE、IME、DPI、多屏与驱动环境仍须分别认证；兼容前端的 Chrome 49 目标也不等于任意旧系统已通过。

<a id="quick-start"></a>

## 开始开发

开发机需要 Windows x64、Visual Studio 2022 C++ 桌面开发组件及 ATL、Windows SDK `10.0.19041.0`、CMake `3.24+`。完整源码工程、现代/Auto 模板和 Workbench 使用 Node.js `22.12+`；基础 SDK 与兼容生成项目保留 Node.js `20.19` 范围。依赖按锁文件安装，首次构建需要网络或预备缓存。

从完整源码根检查环境并构建：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Doctor.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 `
  -Architecture x64 -Configuration Release -Install
```

从兼容模板创建 SDK 目录外的新项目：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/New-Project.ps1 `
  -Name MyDesktopApp -OutputRoot ..\MyDesktopApp
Push-Location ..\MyDesktopApp
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Build.ps1 `
  -Architecture x64 -Configuration Release
.\out\x64\bin\Release\MyDesktopApp\MyDesktopApp.exe
Pop-Location
```

主要编辑 `app/`、`web/`、`contracts/` 和应用资源；SDK 所有的文件由 `sdk-lock.json` 固定。安装器会拒绝同版本不同内容和被修改的受管文件。升级先备份业务工程，再安装整套新 SDK、重新编译并核对实际退出；见[版本与迁移](docs/versioning.md)。

现代与 Auto 项目分别使用 `-Profile modern`、`-Profile auto`。构建完整源码时，用 `-WithWebView2`、`-WithAutoHost` 显式选择对应组件；生成项目再由自己的 `project.json` 选择档位。WebView2 Runtime 由系统或应用部署者另行准备，SDK 包内的 Loader 不等于 Runtime。

## 从页面调用 C++

模板已经注册 `app.greet` 动作；在生成项目的页面中调用：

```ts
import { connectNative } from './native';
import { callAction } from './generated/actions';

async function greet() {
  const client = await connectNative();
  const result = await callAction(client, 'app.greet', { name: 'Developer' });
  console.log(result.message);
}
void greet();
```

原生处理器使用生成的输入与结果类型，注册动作契约并由原生端检查授权和数据。`contracts/actions.json` 是定义来源；构建检查生成的 C++、TypeScript 和描述是否过期。耗时工作使用任务服务的进度、查询和协作式取消；本地超时不代表业务写入已撤销。

React/Vue GUI 绑定帮助组件显式提供 `snapshot`、`perform` 和 `waitFor` 语义。它们借用应用的 SDK 实例，不自动开放本地 Agent 或 Windows UIA。相应入口需要应用单独决定主体、能力和授权。详见 [API](docs/api.md)和 [GUI 绑定](docs/gui-bindings.md)。

## 原生受控 DevTools

DevTools 在 Debug 与 Release 中使用相同的许可规则。模板的 `allow_native_devtools` 默认 `false`，普通 Bridge、CLI、网页、UIA 和环境变量不能把它打开。应用在启动/准备前配置策略，在可信原生菜单处理器中检查自己的身份与策略，并通过默认 No 的确认后发放不透明 grant。

F12 与 Ctrl+Shift+I 只作为当前窗口及其子窗口的原生请求入口，仍经过同一个授权处理器。部署 Inspector、按下快捷键或选择诊断构建档都不等于获得许可。

- MiniBlink 使用经过审查、固定散列且彼此匹配的内核与离线 frontend。ABI 相符不能替代完整配对校验；不要只替换 `node.dll` 或对待验目录现算一个散列再作为信任 pin。
- WebView2 使用其实际 Runtime 的 DevTools。公开 API 无法可靠观察的 `attached`、`paused`、`visible` 保持 unknown；窗口截图不能把原生未知值改成已知。
- 当前内置结束策略为 `destroy_target`。撤销/结束诊断可能关闭当前业务窗口；应用先处理未保存状态，再保留 Host、Bridge、adapter、HWND 与消息循环，直到实际退休及 Runtime 屏障完成。
- `request.accepted()` 表示接纳，不表示附加或退出已完成。超时、失败或未知观察必须保留；资源回收后也不能把旧失败改写成正常完成。

源码 SDK、CMake 安装目录与离线供应材料有不同职责。传统 `Soui::Sdk` 的 CMake prefix 提供编译库和头文件，不会自动包含模板、Node 部署工具或 MiniBlink Inspector。未发布候选中的 `Soui::Desktop` 另安装 `share/SouiDesktop` 下的应用模板、Node 交付工具和前端适配，见[统一桌面入口](docs/unified-desktop.md)。MiniBlink 配对材料的准备、六个显式构建输入和原生接入示例见 [DevTools 文档](docs/devtools.md)。

### 离线源码诊断

```powershell
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release `
  -WebProfile source-diagnostic
```

默认 `distribution` 不带业务源码；`source-diagnostic` 在资产中携带 inline map 与 `sourcesContent`，并保留独立构建/资源身份。MiniBlink 的兼容映射采用有限的 inline map、相对 source 标签和内容身份规则；外置/在线 map 不作为该路径的加载来源。它不是任意文件或网络读取器，也不承诺完整现代 Source Map 功能。

诊断档不提供自动脱敏，不授权 Console。Console 可以使用目标页面原有业务能力；Network、Storage 和源码可能含敏感数据。DevTools 使用、source map 发布边界与真实断点结果按后端、架构、Runtime 和载荷身份分别记录，见[本版范围](docs/releases/0.6.0.md)。

## 性能机制与测量边界

0.6 源码整合有界输入恢复、owner 调度、帧/资源通知及输入到可见反馈的数值关联。可选 WebView2 owner 时间预算、预序列化与 GPU 路径有各自开关和资格边界，默认设置及后端能力检查仍适用。

这些机制用于减少无效工作并支持定位问题。它们没有赋予本版统一的帧率、输入尾延迟、内存峰值或长期稳定性承诺；旧候选的局部数字不迁移为新 SDK 的结果。实际测试方法、原始样本与保留失败见[测试文档](docs/testing.md)和[已知限制](docs/known-issues.md)。

## 构建、打包与退出

```powershell
# 仅编译并安装 C++ SDK
powershell.exe -File tools/Build.ps1 -SdkOnly -Architecture x64 -Install

# 显式选择 AutoHost；验证两个实际启动分支后才能打包 auto 应用
powershell.exe -File tools/Build.ps1 -Architecture x64 -WithAutoHost
powershell.exe -File tools/Test-Auto.ps1 -Architecture x64
```

32 位使用 `-Architecture x86`；Debug 使用 `-Configuration Debug`。模板与 Demo 位于 `out/<架构>/bin/<配置>/`，CMake 安装目录位于 `out/install/<架构>/`。分发时保留完整应用、DLL、`resources/` 与 legal 材料。应用数据放在独立用户目录，部署工具拥有的 `resources/` 会整体替换。

应用 ZIP 保留默认格式；可选 Bundle 按内容去重，须先恢复为完整独立目录。Modern 不包含 WebView2 Runtime；Auto 要求同时保留兼容内核和两份页面资源才能声明离线兼容选择。完整规则见[运行时与打包](docs/runtime-packaging.md)。

最后一个 Host 退出后，应用仍须在原总期限内完成进程级 Runtime 关闭，之后才能清理 COM 等借用者。pending 要继续处理消息，窗口隐藏、进程退出事件或首次提交都不能单独替代实际结束屏障。

## Workbench 与目录

```powershell
.\out\x64\bin\Release\SouiWorkbench\SouiWorkbench.exe --react --page=data
```

Workbench 展示文件扫描、后台任务、媒体界面和组件交互。演示页、历史截图与实验日志分别保留各自版本，不能作为当前发布性能或品牌背书。

```text
sdk/                        公共 C++ / TypeScript SDK 与受控调试实现
templates/desktop-app/      兼容模板
templates/modern-app/       WebView2 模板
templates/auto-app/         可选自动后端模板
demos/workbench/            演示场景与业务适配器
third_party/                固定依赖、源码、声明及来源
tools/                      生成、构建、验证、安装与打包
docs/                       API、迁移、调试与维护合同
verification/               按实际版本和载荷保存的证据
```

## 文档与当前资格

| 开发 | 接入与诊断 | 交付 |
|---|---|---|
| [开始接入](docs/getting-started.md) | [C++ / Web API](docs/api.md) | [版本与迁移](docs/versioning.md) |
| [构建与工具链](docs/build.md) | [DevTools](docs/devtools.md) | [发布流程](docs/releasing.md) |
| [AutoHost](docs/auto-backend.md) | [React / Vue 绑定](docs/gui-bindings.md) | [0.6 发布说明](docs/releases/0.6.0.md) |

本次授权将低收益长测与重复演练后置。D13 千次生命周期、D14 八小时、完整性能对比及未完成的平台/输入环境资格仍保持待验；D01–D12 的未关闭范围也不因发版自动关闭。正常发布分类不表示这些项目已经通过。签名状态、当前构建和资产 SHA-256 以发布记录为准；本项目打包工具不会制造代码签名。

首方 SDK、模板、Demo 和维护工具采用 [MIT](LICENSE.md)。第三方许可独立保留，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)与[0.6.0 对应源码及再分发说明](docs/licenses/v0.6.0-redistribution.md)。完整对应源码资产名为 [`soui-sdk-0.6.0-full-source.zip`](https://github.com/chenziyang110/soui-web-sdk/releases/download/v0.6.0/soui-sdk-0.6.0-full-source.zip)，最终封存与下载状态见发布说明和实际附件；配对 Inspector 与内核还需保留其原始来源和批准材料。问题反馈见 [Issues](https://github.com/chenziyang110/soui-web-sdk/issues)，安全问题遵循 [SECURITY.md](SECURITY.md)。
