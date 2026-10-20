# 统一桌面入口：同一份 C++ 与 Web 代码

`Soui::Desktop` 提供独立于 SOUI/Miniblink 的单主窗口应用入口。相同
`soui::desktop::application` API 和本地 HTML/CSS/JavaScript，可选择
GTK3/WebKitGTK、AppKit/WKWebView 或 Win32/WebView2 后端。业务代码不需要平台
`#ifdef`。示例位于 [examples/unified-desktop](../examples/unified-desktop)。

这是未发布开发功能，不是 Electron 全功能替代或“全部架构完美运行”声明。
Windows/macOS 代码尚未目标编译；Linux 已真实编译链接，但 GUI 集成受环境限制。
正式分发前必须完成目标机验收与版本、许可、交付清单更新。

## 源码路径、依赖与本轮证据

| 平台 | 桌面目标 | 后端与最低前提 | 本轮证据 |
|---|---|---|---|
| Linux | x64、ARM64 | GTK 3.24+、WebKitGTK 4.1 2.40+、C++17、正常图形会话 | Debian 13 x64 / GCC 14.2 / GTK 3.24.49 / WebKitGTK 2.52.6 编译链接、安装消费者通过；GUI 未通过验收；ARM64 未编译 |
| Linux | x86、ARM32 | 仅当目标发行版提供上述架构的完整开发/运行包；需正确 ISA/浮点 ABI/sysroot | 未编译或 GUI 验证，不承诺所有 32 位发行版可用 |
| macOS | x64、ARM64、universal2 | macOS 12+，Apple SDK，Objective-C++17/ARC，AppKit、Foundation、WebKit、CoreFoundation | 源码实现与审查；未编译/运行 |
| Windows | x86、x64、ARM64 | MSVC-compatible C++17、Windows SDK、匹配架构 WebView2 SDK loader、当前受支持 Evergreen Runtime | 源码实现与审查；未编译/运行 |
| Windows ARM32、macOS x86/ARM32、ARM64EC | 不支持 | 不伪造缺失的后端发行物或现代 32 位 macOS 支持 | 配置拒绝 |

Windows 后端必须能查询 `ICoreWebView2_18`，否则初始化失败关闭；接口引入于 SDK
1.0.1823.32 / Runtime 114。该接口版本不是继续使用过期 Runtime 的建议，部署应使用
当前受支持的 Evergreen Runtime/Windows 组合。构建不自动下载 SDK 或运行时。
[Microsoft 接口版本说明](https://learn.microsoft.com/en-us/microsoft-edge/webview2/release-notes/sdk/1-0-1823-32)

`tools/platform-info.cjs` 的 `system_desktop` 表示此源码/构建路径，`desktop_host`
仍表示原 Windows SOUI/Miniblink 路径；两者都不能替代依赖探测或 GUI 实测。

## 同一份构建入口

先自行准备对应平台开发依赖，然后从仓库根目录：

```sh
cmake --preset desktop
cmake --build --preset desktop
ctest --preset desktop
cmake --install out/desktop --config Release
```

Linux 示例依赖包名（Debian 系，具体版本以发行版为准）：
`libgtk-3-dev libwebkit2gtk-4.1-dev pkg-config`。不要设置
`WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS` 或降低系统安全设置来绕过运行失败。

Windows 配置时提供官方 Microsoft.Web.WebView2 SDK 完整解包目录，内含
`build/native/include/WebView2.h`、对应 `x86/x64/arm64` loader 及许可文件：

```powershell
cmake --preset desktop -A ARM64 -DSOUI_WEBVIEW2_SDK_ROOT="C:/SDK/Microsoft.Web.WebView2"
cmake --build --preset desktop
ctest --preset desktop
```

其他架构用独立构建目录和对应 `-A`。不能把 x64 loader 链接到 ARM64。Windows
旧版 SOUI/Miniblink 入口继续使用原 `Build.ps1`；不要把旧缓存改造成新桌面缓存。

macOS 使用 `cmake --preset desktop-macos-universal2`，后续 build/test 同名。
薄片目标可显式设置 `CMAKE_OSX_ARCHITECTURES=x86_64` 或 `arm64`；
最低部署版本为 12.0，调用方明确指定更高版本时保留该值，不静默降低。
universal2 仍需要分别执行两个 slice，不以单个 slice 成功代表全部。

示例可执行文件位于生成器对应的 `examples/unified-desktop` 输出目录，`web/`
会复制到其旁边。也可显式传资源目录：

```sh
out/desktop/examples/unified-desktop/soui_desktop_example examples/unified-desktop/web
```

Windows 多配置生成器对应 `Release/soui_desktop_example.exe`。该示例同一份源码
涵盖 Promise JSON 回显、原生事件、窗口标题/尺寸、剪贴板和原生目录选择。

## 独立安装消费者

完成上述安装后，示例可完全通过已安装包构建，不借用 SDK 源码头文件：

```sh
cmake -S examples/unified-desktop -B out/desktop-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/out/desktop-install"
cmake --build out/desktop-consumer --config Release
```

macOS 消费者同时设置 `-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` 或不低于包构建值的
版本，以及匹配架构。Windows 保持工具链/CRT 一致。目标包依赖本机对应系统
WebView 开发/运行库，不提供跨 OS/编译器的稳定 C++ 二进制 ABI。

业务接入仅需 `find_package(SouiDesktop CONFIG REQUIRED)` 和
`target_link_libraries(app PRIVATE Soui::Desktop)`。公共头文件是
`<soui/desktop/app.h>`；它不含 HWND、GTK、COM 或 Objective-C 类型。

## 公共行为与失败

- `initialize/run/close` 管理单个主窗口；不能同时创建多个 application，也不能从
  回调里销毁 application 对象。`close` 可在回调中请求；销毁必须在创建 UI 线程。
- `navigate/reload` 仅加载原生指定资源根中的本地普通 HTML，规范化并拒绝根外
  symlink、父遍历、网络 URL。重新加载轮换系统熵令牌，旧消息与响应不得跨页面。
- `window.souiDesktop.invoke(method, params)` 返回 Promise；原生 `callbacks.invoke`
  明确实施方法 allowlist 和权限，返回一个 JSON 值。没有默认远程 shell、任意文件
  读取或系统控制命令。`emit` 与 `souiDesktop.on` 支持原生到 Web 的事件。
- 消息限制为 64 KiB UTF-8、最大 JSON 深度 32；页面最多 256 个未完成请求，
  30 秒超时。网页卸载拒绝 pending Promise，非法令牌/来源的消息不获回应。
- UI API 只允许创建线程；后台通过 `dispatch` 投递 UI 工作，最多 1024 项，
  关闭后不执行。业务 invoke/ready/error/closed/work 回调经公共队列串行化，防止
  原生模态消息泵重入业务回调。应用必须活过所有后台调用者。
- 剪贴板文本上限 1 MiB UTF-8，目录选择取消返回失败/取消状态。错误 `code`
  包含共用错误与后端 `E_*`，不应假定不同系统的细节码完全相同。
- `get_capabilities()` 是实际实现能力，缺失能力不会返回空成功。`evaluate`
  属于私有后端接口，不开放任意远程页面的原生特权。

## 内容信任与引擎差异

本入口针对应用拥有的本地页面。外部网页导航、新窗口、下载、外部协议启动、
默认设备授权均拒绝；不把远程网址作为带 Bridge 权限的应用入口。

Windows/macOS 使用原生主 frame/来源信息。GTK 的消息信号没有 sender frame，
因此诚实记录 unknown，依靠顶层注入的每导航随机闭包令牌及当前文档上下文。
这不是同源 frame 隔离：同源/about:blank/srcdoc 子帧、顶层第三方脚本、XSS
或同 realm monkeypatch 可能取得顶层应用能力，必须视同受信应用代码。
不要加载不受信任脚本；示例 CSP 禁止网络、iframe、对象与外部连接。

主 HTML 根边界检查不是完整文件系统沙箱；它不证明所有脚本、CSS、图片资源
均在根内，也不消除资源目录被其他进程改动的 TOCTOU。保护资源目录完整性、
限制业务方法权限是应用责任。既有 Runtime 的本地 IPC 身份认证要求不改变。

GTK 保守拒绝无原生 ticket 的同 URI `location.assign` 等完整导航，明确
renderer reload 则转公共重载；应用主动导航应走原生 API。只有引擎确认为真正
同文档的 fragment 更新保留当前会话。WebKitGTK 的 file-origin 防护保持开启，
任意本地 fetch/ES module 或 Vite 绝对资源路径不保证与 Electron 相同；需使用
适合本地页面的资源输出，后续 root-scoped 资源服务应单独设计和验证。

字体、CSS、媒体编解码、焦点、输入法、DPI 和渲染时序随系统引擎变化。当前尚无
多主窗口、统一托盘/菜单/快捷键、跨平台 accessibility/Agent endpoint、自动更新、
安装签名或完整 Electron API 兼容。macOS 默认非持久 WebKit 会话且不支持任意
`user_data_directory`（明确报错）；Windows/Linux 的数据目录策略见后端说明。

## 实际验证和待验收

本轮成功：Linux x64 真实头库编译/链接、独立安装包消费者构建、公共
application/protocol 的 fake-backend 契约测试、真实 preload 的 Node VM 测试。
fake 和 VM 不构成真实浏览器或窗口验收。

本轮 GUI 阻塞：执行环境不能创建 Xvfb/D-Bus 所需 Unix socket，受支持的提升
执行入口也无法初始化。没有修改权限或关闭 WebKit sandbox。原生二进制启动
在无显示服务时明确返回 `E_DISPLAY`，不是通过或 skip。

在已有正常图形会话和依赖的 Linux 目标机：

```sh
cmake --preset desktop -DSOUI_DESKTOP_GUI_TESTS=ON
cmake --build --preset desktop
ctest --preset desktop -R sdk.desktop.linux-native --output-on-failure
```

原生测试有真实页面/JavaScript/Bridge、剪贴板、目录取消、线程调度、重载和
关闭用例，缺少显示服务时必须失败。Windows/macOS 编译及这些交互都尚未执行。
完整本轮结果见 [2026-10-20 实际记录](acceptance/unified-desktop-2026-10-20.md)。
具体检查清单见 [Linux](../tests/portable/desktop-linux.md)、
[macOS](../tests/desktop/macos-wkwebview.md) 和 [ADR-0119](adr/0119-unified-system-webview-desktop.md)。
