# PR #103 六纵切集中限定验证：2026-10-23

代码基线：[远端提交 224276096847e10a53250f46756c8aa941e9e535](https://github.com/chenziyang110/soui-web-sdk/commit/224276096847e10a53250f46756c8aa941e9e535)。
19 个改动文件远端 blob 与本地冻结快照 `de23e8f38c868a383829c0ea6cd4c9f9d9f332ee` 全部一致，
对应阶段二打开/保存路径选择完成的源码。
本记录不修改生产 acceptance.json，不构成 releaseReady、签名、安装器或全架构通过结论。

后续限定证据追加至 2026-10-26；各段绑定各自源码基线，历史结果不被覆盖。

## 实现范围

| 纵切 | 当前实现 | 未完成的验收 |
|---|---|---|
| 窗口状态 | 三后端 minimize/maximize/restore、观测状态和事件、网页显式授权 | 实际窗口管理器状态、焦点/DPI/IME |
| 可取消关闭 | 宿主同步审批、整组预检、重入屏障；程序 close/quit 保留 force | 三端标题栏/系统退出、未保存业务交互 |
| 受控服务 | 当前窗口属性、剪贴板读/写、目录选择独立默认拒绝，typed facade | 最新三端真实剪贴板和模态交互 |
| 层级菜单/托盘 | 64 节点/4 层、禁用祖先与命令修订隔离 | 目标桌面菜单与托盘宿主行为 |
| 全局快捷键 | 宿主注册，Windows/Carbon/X11，窗口所有权及注销屏障 | 真正注册、冲突、触发、释放；本轮未操作用户全局热键 |
| 打开/保存路径 | 三端 native chooser、独立权限、明确取消和路径限额 | 目标对话框交互；选择路径不授予读写，不执行文件操作 |

## 云端本次实际执行

Linux x86_64 / GCC，真实 GTK 3.24.49、WebKitGTK 2.52.6 开发头文件来自既有隔离依赖。
只运行下列限定组合，不重编 WebKit，不运行远程 CI，不冒充原生 GUI。

1. 冻结源码重新编译共用层，并完整执行一次 fake contract：**1321 checks，0 failures**。
   覆盖六纵切与既有公共生命周期/Bridge 契约，不调用三端原生 UI。
2. Node 五个工具/adapter 文件：**39 pass**；真实 preload 生成脚本在 Node VM：**17 pass**。
   合计 **56 pass、0 fail、0 skip**。初次组合命令漏传 contract 路径，preload 在启动前失败；
   正确传入后只补跑该套，保留原始调用错误，不将其算成产品断言失败或隐去。
3. Linux 后端使用 `-fsyntax-only -Wall -Wextra -Wpedantic -Werror`，X11 宏开启/关闭各一次通过。
   初次将外部头文件作为普通 `-I`，glibc 的 `#include_next` 在 pedantic 下触发错误；
   改为标准 `-isystem` 分类外部依赖后通过，未修改或关闭 SDK 警告。
4. CMake 3.31.10 / Ninja 全新 desktop 配置通过（`SOUI_BUILD_TESTS=OFF`），
   生成的安装规则包含 helper、6 个工具、模板、adapter 和声明，共 10 项路径检查通过。
   这是配置/安装规则核对，**不是本轮 Linux SDK 构建或安装执行**。

### 可复现命令

在仓库根目录，将 `WORK` 设为独立临时输出目录：

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  -Isdk/include -Ithird_party/include/json tests/desktop/contract.cpp \
  sdk/src/desktop/{application,protocol,assets,asset_executor,resource_server}.cpp \
  sdk/src/runtime/platform/secure_random.cpp -o "$WORK/contract"
"$WORK/contract"
node --test tests/desktop/frontend-adapter.test.mjs tests/desktop/create-app.test.cjs \
  tests/desktop/stage-app.test.cjs tests/desktop/verify-app.test.cjs tests/desktop/archive-app.test.cjs
SOUI_DESKTOP_CONTRACT_EXECUTABLE="$WORK/contract" node --test tests/desktop/preload.test.cjs
```

Linux 语法检查用 `pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 gdk-x11-3.0 x11`，
把依赖 `-I` 选项转换为 `-isystem`，添加 `-Isdk/include -Ithird_party/include/json`；
对 `sdk/src/desktop/backends/linux_webkitgtk.cpp` 分别省略/设置
`-DSOUI_DESKTOP_X11_SHORTCUTS=1`。隔离依赖需要准确设置 PKG_CONFIG_SYSROOT_DIR/LIBDIR，
不得将本地测试绝对路径写入交付工程。

CMake 使用 `cmake --preset desktop -B "$WORK/cmake" -G Ninja -DSOUI_BUILD_TESTS=OFF`；
本环境显式指定已有 Ninja 路径。检查生成的 sdk/desktop/cmake_install.cmake 中
SouiDesktopApplication.cmake、stage/sync/preflight/verify/archive/create 工具、模板及两个 frontend 文件。
工具套件包含合成 ELF/Mach-O/PE 元数据和临时目录，不能等同实际目标二进制启动。

## 第六纵切 macOS 原生编译

Mac 子任务核对 9 个增量文件 blob 与上述基线一致。显式 Xcode SDK 配置退出 0，
`cmake --build out/desktop-universal2 --target soui_desktop soui_desktop_example soui_desktop_contract --parallel 1`
完成 17/17、退出 0，包含打开/保存对话框 Objective-C++ 后端与新增系统框架链接。
静态库 `lipo` 包含 x86_64、arm64；ARM 宿主执行 `--file-dialogs` 为 351 checks、
0 failures。这是 fake backend 契约，不打开真实面板、不读取/写入用户文件，不验证 GUI。
环境与显式 SDK 路径见 [Mac 环境记录](../macos-native-validation-20261023.md)。
### 最终已安装 SDK → 独立 universal2 消费者

最终执行前核对 adapter：`desktop.mjs` blob 为 `863ee15c30a7e4c919386b056b2e66c851458c27`，
`desktop.d.mts` 为 `cf070f71780edf2d915f21da2868ff906119f7c2`。
源码、安装 SDK、生成项目 web/soui 中这两个文件逐字节/散列一致，安装头文件同源。

- 新安装目录 `out/desktop-sdk-delivery-final`：53 文件，7,020,830 字节。
- 新生成目录 `out/desktop-delivery-consumer-final`：12 文件；配置显式指定
  `-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'`，库及消费者 lipo 均有两个 slice。
- 安装、生成、配置、构建、stage、verify、archive 全部退出 0。
- `out/desktop-delivery-staged-final-universal2`：12 个 manifest 文件，1,406,037 字节。
- `out/desktop-delivery-archive-final-universal2`：ZIP 1,414,285 字节，SHA-256
  `7167f787d8ea585a5ab3dc2fb421861ec7369e8355c93f2018286887e4e0db63`，sidecar 一致，
  `integrityVerified=true`、`nativeRuntimeVerified=false`。没有签名或真实性认证声明。

保留前置执行失误：首次未同步最新 adapter，使用旧 frontend 生成 arm64 消费者；
首次错误标记 universal2 被校验拒绝。旧输出保留不覆盖，正确同步并在全新目录显式
选择双架构后才获得以上最终结果。此为执行接线错误，不据此报告产品源码缺陷，
也不将旧包数字混入最终通过结果。未运行生成程序 GUI、真实对话框/热键或 Intel slice。

## 生成应用启动诊断与架构身份校验（后续限定检查）

本节源码基线为远端提交
[`14bc147f2ee424cca9763a2a53ec7583d0d81660`](https://github.com/chenziyang110/soui-web-sdk/commit/14bc147f2ee424cca9763a2a53ec7583d0d81660)，
17 个文件已逐 blob 核对；不将此前六纵切的结果自动推广到本次模板变更。

生成模板新增去敏 `--diagnose`、启动失败原生提示及即时首错 stderr。诊断模式不调用
业务回调、不进入应用事件循环，但可能创建隐藏原生窗口/WebView 与短暂本地监听器；
成功仅表示有限初始化完成，不代表 GUI、Bridge 或 `nativeRuntimeVerified`。
原生提示为 best-effort，显示条件不满足时保留 stderr。错误输出不包含原始任意消息、
资源根路径或带凭据的 origin。加载器在进入 main 前失败仍需现有 doctor 等外部诊断。
同时统一 thin/fat Mach-O 的 generic CPU subtype 校验，拒绝把 arm64e、x86_64h 或
未知 subtype 错标为通用目标。

### 云端限定检查

- `node --test tests/desktop/create-app.test.cjs`：6/6。
- `tests/desktop/startup_report.cpp` 的 C++17 编译及执行通过，覆盖报告去敏和实际
  模板 main 的 fake SDK 流程，包括有限初始化、立即首错日志与退出后提示顺序。
- Linux 提示 helper 使用现有 GTK 头文件的严格语法检查通过，非原生提示运行。
- staging：13/13；verify/archive：10/10。新增 subtype 案例已验证旧实现失败、
  新实现通过；这些合成二进制检查不是目标平台执行证据。
- 独立审查、`git diff --check` 与暂存差异检查通过。未重跑全量回归或 CI。

### macOS 最新模板编译与 staging

沿用上文 Mac 工具链与显式 Xcode SDK 26.5。首轮大 base64 传输失败且未写源码，
不计作编译失败；随后 8,804 字节 gzip 小补丁核对 SHA-256
`4c2e14a2664bc8bf90059b0f74ddf339129fea950183dd31f9c7a4c46e9ee7fb`、应用退出 0，
9 个增量文件 blob 全部匹配本节基线。

- 已有 `resource-server-diagnostic-sdk-build` 重新安装 Release，退出 0。
- 通过安装 SDK 生成全新 `StartupDiagnosticConsumer`，16 文件、退出 0；配置显式
  `arm64;x86_64`、SDK 26.5 及最新 SouiDesktop/SouiRuntime，退出 0。
- 编译 `main.cpp`、`startup_dialog_macos.mm` 并链接成功，退出 0。
- 源码、安装 SDK 与生成模板在替换占位符后内容一致；两个 frontend 文件一致。
- 消费者与两个 SDK 静态库的 `lipo` 均确认 x86_64、arm64 两个 slice。
- stage 与独立 verify 均退出 0，结果 `integrity_consistent`：12 个 manifest 文件，
  1,441,901 字节，`diagnosticsPresent=true`、`nativeRuntimeVerified=false`、`signed=false`。
- 产物为 `out/startup-diagnostic-consumer-staged/StartupDiagnosticConsumer.app`；
  原 DeliveryConsumerFinal 与手动诊断包保留，未覆盖。

本次未运行应用、`--diagnose` 或 GUI，未签名；原生提示的实际可见性、Finder 启动、
真实窗口/Bridge/系统服务交互仍未验收。编译和交付通过不表示旧诊断包的启动根因已解决。
在该次历史检查时，Windows 原生构建、新增 Linux GUI 路径及其他未测架构仍缺证据；后续结果见下文。

## 平台/架构事实矩阵

| 平台/架构 | 实现/目标策略 | 当前原生证据及缺口 |
|---|---|---|
| Linux x64 | GTK/WebKitGTK；X11 快捷键按依赖启用 | 历史真实 GUI/Bridge/ESM/fetch 已过；当前六纵切仅公共回归及后端语法检查，运行时快照丢失，未重跑 GUI |
| Linux ARM64/x86/ARM32 | 架构策略存在，32 位取决发行版完整依赖 | 未目标编译/运行；不能由 x64 或合成矩阵推断 |
| macOS arm64/x64/universal2 | WKWebView，现代 64 位目标 | universal2 构建及安装交付通过；2026-10-24 新候选在 Apple Silicon 主机限定窗口/Bridge/模块/fetch/关闭和 DOM 否决探针通过；Intel slice 未运行，完整系统交互未验收 |
| Windows x64 | WebView2 实现及目标策略 | 2026-10-24 Release / MSVC 构建安装、原模板与独立探针交付通过；真实 WebView2 Bridge/模块/fetch/公开关闭探针通过；2026-10-26 原模板可见窗口/app.info/模块/fetch、原生标题栏最大化/还原及正常窗口关闭通过；原模板进程退出码和完整交互未测 |
| Windows x86/ARM64 | WebView2 实现及目标策略 | 未目标编译/运行，不能从 x64 推断 |
| Windows ARM32 桌面、macOS 32 位、ARM64EC | 不作为此统一桌面支持目标 | 明确拒绝/不支持，不标未测试即可支持 |

Linux Wayland（包括处于 Wayland 会话中的 XWayland）及未知会话的全局快捷键明确
`not_supported`，尚未实现 Portal。应用内 accelerator、任意文件读写/持久授权、
自动 updater、安装器、签名/公证和完整 Electron API 等价不是本轮已实现能力。
服务能力可用不等于特定快捷键/窗口管理请求必成功，页面预授权不等于真实系统手势。

## 收口状态与下一步

阶段一/二六项核心代码已实现，最新 Mac 原生构建及安装 SDK → 生成 universal2 消费者
→ stage/verify/archive 已通过。限定集中非 GUI 交付检查完成，但阶段三真实平台验收仍未闭环。
最终 DeliveryConsumerFinal 的 Finder 无窗口现象根因尚未确认。普通直接执行已捕获
资源监听 bind 失败，但不能据此确定 Finder 启动根因。旧 SouiMacVerify 的 AppKit
激活失败仅作为旧包历史证据，不套用于最终包。不重复提权、绕过系统保护或把 fake
运行当 WKWebView 通过。

Linux 历史证据见 [2026-10-22 记录](../linux-native-validation-20261022.md)，
Mac 环境/历史交付与 GUI 阻塞见 [2026-10-23 记录](../macos-native-validation-20261023.md)。
旧记录均保留自己的基线；新路径策略及六纵切不能继承历史 GUI 通过结论。
后续 Mac 新候选窗口/资源及 DOM 关闭否决证据见 [ADR-0145](../adr/0145-observed-macos-activation-policy.md)
和 [ADR-0138](../adr/0138-cancellable-user-close.md)；Windows x64 限定证据见下节。
仍需同业务菜单/系统服务等完整原生交互，以及其余未测架构证据。
在这些门槛关闭前保持 draft；不更改版本/许可、不合并或发布、不声称“一切平台架构完美运行”。

## Windows x64 Release：限定原生验证（2026-10-24）

源码基线为 [220aed8ce822d7f60fb50da3d7fefd7b90869954](https://github.com/chenziyang110/soui-web-sdk/commit/220aed8ce822d7f60fb50da3d7fefd7b90869954)，SDK 工作树无修改。
目标报告于 18:13 UTC 完成；本轮未发现或修复 SDK 生产源码缺陷。

### 工具链与执行范围

- 主机报告 Windows `10.0.26200` x64，构建为 Release；MSVC `19.44.35229`、
  Windows SDK `10.0.19041.0`、CMake `3.31.6-msvc6`、Ninja `1.12.1`。
  实际生成器为 Visual Studio 17 2022，`-A x64 -T v143`，选择 SDK 19041；
  SDK tests/example 为 OFF，不把安装的 Ninja 当成本轮实际生成器。
- WebView2 SDK `1.0.4258.31`、实际 Runtime `155.0.4283.62`。
  官方安装器阶段 17:26:40–17:27:02 UTC 退出 0；包装回执曾因
  `Set-Content -NoClobber` 报错，但安装只执行一次，捕获进程退出结果及实物签名核对成功。
  三项组件阶段 17:30:48–17:44:59 UTC 退出 0，无重启。
- 早期隔离构建因 Ctrl-C 返回 1，不计为通过；后续正常主机 SDK configure/build/install
  均为 0。原模板与专用探针分别生成、configure/build/stage/verify，以及
  `validate-native-run.cjs` 均为 0。不包含完整契约套件或旧 SOUI/Miniblink 路径验收。

以下路径相对目标机本任务目录，不写入用户名或绝对开发机路径：SDK 源码为
`soui-web-sdk-win103-src`，其构建/安装为 `out/desktop`、`out/desktop-install`；
原模板项目 `SouiWindowsConsumer`，专用探针项目 `SouiWindowsConsumerProbe`。
两者均生成 17 个源文件，交付可执行文件位于各自
`build/staged-Release/SouiWindowsConsumer.exe`。该次原模板未修改、未启动；仅探针改动：

| 探针源文件 | 行数 | SHA-256 |
|---|---:|---|
| main.cpp | 149 | `3a20455cf3e1c9a4f37e8f5cdbec9ff15a8588a8fb52aa863512f933943f4b70` |
| web/app.mjs | 25 | `56cac00605016f21412f6fdd8d628d7cce9eff34b40aee9390798b7d107f5d89` |

### 交付身份与真实运行

| 载荷 | verifier 文件/字节（不含清单） | 可执行文件字节及 SHA-256 | 清单 SHA-256 |
|---|---|---|---|
| 原模板 | 11 / 584,416 | 557,056 / `73830ace63aab4db1285f8639881b33b2b79409ebe1d61786b90628a8f7af343` | `507af2061034094110a1bba1ba8dc98b99bc2a78665b083574d1f485efc8ce54` |
| 专用探针 | 11 / 586,793 | 559,104 / `bd01d2b3206f236c64c6f28c8c67e17c2985b7e4dd6a432c8b06ccbfc20eb46b` | `02a4d5ebc28d066f0bcc59fe4b47743835c3a629c62f41f844938c2d156aa785` |

两份清单各 2,572 bytes，含清单的实际目录分别为 12 文件 / 586,988 bytes、
12 文件 / 589,365 bytes。两者 `nativeRuntimeVerified` 仍为 `false`，不篡改
通用完整性清单以替代独立运行证据。

仅启动探针的确切交付 exe，工作目录为该 staging 目录，`Start-Process` 使用
`WindowStyle Normal`，应用 `settings.visible=true`，无参数、无 `--diagnose`。
这表示请求可见窗口，不是人已看到窗口的证据。运行从
`18:06:11.9300535Z` 至 `18:06:16.9337970Z`，`WaitForExit(45000)` 返回 true，
进程及外层执行均退出 0，无强制终止。stdout 为 799 bytes，SHA-256
`c8d5ba01257250d01f4905ba1b11852b495da510d7cc31e5b2d391d0a9ee720f`；stderr 为 0 bytes。

报告 schema 为 `soui.windows-native-probe/1`：
- `native_ok/resources/constructed/destroyed/initialized/run/quit/backend_webview2/close_dispatch/close_request/watchdog_joined` 均为 true。
- `info/report/finish/ready/closed/approvals` 均为 1，`errors/page_errors` 均为 0。
- `timed_out/timeout_dispatch_failed/unexpected` 均为 false，`failure_stage/code` 为空，`page_failure=null`。
- 页面实际 `app.info` 为 identifier `org.soui.windowsconsumer`、name `SouiWindowsConsumer`、version `0.1.0`；
  加载链为 static module → dynamic module → same-origin fetch，随后公开关闭路径和正常销毁完成。

本机保存的 `handoff.md` 为 11,814 bytes，SHA-256
`27d7c5a58b201c35080e255c54454f6021211144946295665064afc21f286951`；
`windows-native-logs/windows-x64-evidence-handoff.json` 为 21,996 bytes，SHA-256
`6ea2bd6bfb1b5083925140ef79adc6b344d2f1771431ffaed9f01a790865a2c9`。
这些证据只保存在目标任务目录，未作为仓库附件上传，以上是结果及身份记录，不假造可下载附件。

2026-10-24 该次无截图或视觉人工交互；公开关闭探针不是 DOM 关闭否决、系统退出或全部六纵切验收。
x86/ARM64、其他 Windows 版本、菜单/托盘/快捷键/剪贴板/对话框等原生交互仍未覆盖。
不据此宣称正式分发、签名、生产就绪或全部平台架构通过。

<a id="windows-original-template-gui-20261026"></a>

## Windows x64 原模板可见 GUI 补充（2026-10-26）

本节复用源码基线 `220aed8ce822d7f60fb50da3d7fefd7b90869954` 的既有 SDK 与原模板
Release 交付物；本轮未改源码，未重复安装、编译、stage/verify 或探针。
本节文档基线是 `27b146c013500f4d38093c8fc6be9fb3ab931a8e`，它相对前者仅修改四份文档；
不能将文档提交写成已编译或运行的源码身份。原 stage 清单的 `nativeRuntimeVerified=false`
及生产 `acceptance.json` 均未修改，不作 `releaseReady` 声明。

### 交付身份

主机 DESKTOP-4URQ5CB，Windows x64 Release，现有 WebView2 Runtime 文件版本
`155.0.4283.62`（只读元数据），Computer Use skill `26.1021.31345`。
下列路径相对 Windows 本任务目录：原模板 exe 为
`SouiWindowsConsumer/build/staged-Release/SouiWindowsConsumer.exe`，清单为同目录
`application-manifest.json`。2026-10-26 `06:07:33.0304777Z` 的只读核对退出 0，
源码工作树干净；11 个清单文件 / 584,416 bytes 均匹配大小和 SHA-256。
包含清单的目录仍为 12 文件 / 586,988 bytes，与上节交付物一致。

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| SouiWindowsConsumer.exe | 557,056 | `73830ace63aab4db1285f8639881b33b2b79409ebe1d61786b90628a8f7af343` |
| application-manifest.json | 2,572 | `507af2061034094110a1bba1ba8dc98b99bc2a78665b083574d1f485efc8ce54` |

### 实际窗口操作与结果

本轮使用受支持的原生 CUA 接口，无 shell UI 自动化、注入、代批或安全设置变更。

1. `06:05:02.853Z` 原生应用和窗口清单均成功返回，均无匹配目标。唯一正常启动
   `cua.computer.launch_app` 于 `06:05:34.036Z` 发起，`06:05:41.160Z` 返回成功；
   精确目标为上述原模板 exe，无参数、无 `--diagnose`，本轮无审批错误或已知有效待批。
2. `06:05:48.990Z` 找到唯一窗口 67808，标题 `SouiWindowsConsumer`，process 标识匹配该
   stage exe。实际截图显示页面及初始文字 `Frontend module loaded. Choose a check above.`。
3. 点击真实页面 `Read application info`，可访问文本和截图均显示 identifier
   `org.soui.windowsconsumer`、name `SouiWindowsConsumer`、version `0.1.0`。
4. 点击 `Load module and JSON`，页面显示 `static module → dynamic module → same-origin fetch`，
   config.application 为 `SouiWindowsConsumer`，message 为
   `Edit this frontend without adding platform branches.`。这是实际页面的结果，不是静态源码推断。
5. 操作原生标题栏“最大化”及“还原”：截图尺寸为
   `1026×752 → 1920×1032 → 1026×752`，模块结果保持可见。
   **这只验证原生标题栏交互，未验证网页 window-controls API 或状态事件。**
6. `06:10:33.170Z` 点击原生关闭按钮，`06:10:33.286Z` 返回。随后状态捕获报告
   `window is not a usable app window`；`06:10:40.418Z` 原生窗口和应用清单均无匹配项。
   通过的是正常窗口关闭及窗口/CUA 应用条目消失；未强杀。
   **CUA 未提供进程退出码，不能推定 exit 0、进程析构或全部关闭稳定性通过。**

### 截图、报告与历史边界

5 张未经编辑、仅含本应用的真实 JPEG（初始、app.info、模块/fetch、最大化、还原）
仅留存在同一任务 `01a16ebd-4aed-7366-8d42-1b27ebbce009` 的原生 CUA 图片输出。
没有独立本地图片路径，也没有返回的附件或 Library ID；本节不假造仓库图片或下载链接。

本机任务目录保存 `windows-pr103-original-consumer-gui-pass-20261026.md`
（4,825 bytes，SHA-256 `c72c266cc3e7906bc5a467250e256ab3ddfe2aa09d4b310ee02e3819caad7793`）
和 `windows-native-logs/original-consumer-gui-pass-20261026.json`
（7,734 bytes，SHA-256 `d4310c58319ed97bd385736be916e25f65785d533507b872561cf9fb6496b227`）。
这些是本地证据身份，不表示文件已作为仓库附件上传。

保留此前 GUI 阻塞：2026-10-25 的两次原模板启动请求均因
`Computer Use app approval timed out` 未获得成功回执；其中第二次为
`16:02:21.301Z–16:07:21.323Z`。随后的原生清单未找到目标，执行结果仍未确认。
旧请求已超时，不是当前待批；本轮成功不改写这些历史失败，也不改写旧 Mac 包的失败。

原模板进程退出码、网页窗口控制 API/事件、DOM 关闭/否决、多窗口和系统退出、
最小化/还原、手动移动、焦点/DPI/IME/多屏、菜单/托盘/全局快捷键、剪贴板及
目录/打开/保存路径对话框、长稳、其他架构/配置/Windows 版本及完整平台矩阵仍待验收。
保留 draft、既有版本/许可和全部生产/分发门槛，不据此签名、合并或发布。
