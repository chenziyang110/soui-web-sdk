# 统一桌面入口：同一份 C++ 与 Web 代码

`Soui::Desktop` 提供独立于 SOUI/Miniblink 的受管理多窗口应用入口。相同
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
新增示例按钮可创建子窗口、显示原生菜单和设置托盘；示例显式持有子窗口句柄，
不依赖全局裸指针或网页任意窗口访问。

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

## 统一开发打包入口

同一条命令适用于三端已配置的桌面构建目录（需要 Node.js 18+、CMake、CTest，
以及对应目标工具链/开发依赖）：

```sh
node tools/package-desktop.cjs --build-dir out/desktop --output out/desktop-package-01
```

多配置生成器可加 `--config Debug`；单配置生成器必须与缓存的 `CMAKE_BUILD_TYPE`
一致。输出目录的父目录须存在，输出目录本身必须不存在，不会覆盖旧包。
入口依次执行构建、全部已注册 CTest、安装和 ZIP，任一失败立即非零退出，
保留 `FAILED.json` 和现场，不继续交付。跨编译但无法执行目标测试时也会失败，
不会把构建宿主 CPU 冒充目标或跳过测试。

输出包含安装树 `sdk/`、内容摘要命名的 `*-dev-*.zip` 和 `.sha256`。安装树内
`development-manifest.json` 记录实际目标 OS/架构/后端、逐文件散列与明确的
`releaseReady=false`。它是可用 `CMAKE_PREFIX_PATH` 消费的 SDK 开发快照，不是
可双击的应用安装包；不包含系统 WebView、签名、公证、自动更新或 GUI 认证。
其 `registeredTests=passed` 仅对应实际注册测试，不能推导未注册 GUI 测试通过。
不同内容有不同开发包摘要，不修改现有 0.5.0 Release。

## 多窗口、原生菜单与托盘

根 `application` 初始化后，通过 `create_window(options, callbacks)` 创建子窗口；
检查 `window_result.status`，并持有成功返回的 `shared_ptr<application>`。
同一组最多 32 个活动窗口，每窗具有稳定且不复用的 `id()`、独立资源根、
回调、令牌和导航代际。`window_ids()` 返回仍活动的窗口 ID。

根对象持有整个应用组的生命周期，只有根对象调用 `run()`；关闭根窗口本身不影响
仍打开的子窗口。`close()` 只关当前窗口，最后一窗关闭才退出循环；`quit()` 关闭
整个组。根对象析构会关闭剩余窗口，外部尚持有的子窗口句柄失效。销毁必须在
创建 UI 线程，不能在业务回调中直接析构对象。

`popup_menu(vector<menu_item>)` 显示平面原生弹出菜单，`set_tray(tray_options)`
设置窗口所属托盘，`clear_tray()` 移除。命令通过该窗口的 `callbacks.command`
回调交付，`callbacks.lifecycle` 提供带窗口 ID 的 ready/closed 事件。菜单最多
64 项，命令 ID 唯一有界、文本必须有效 UTF-8；菜单栏、嵌套子菜单和快捷键
不在本轮契约内。这些接口仅供原生业务显式调用，不给网页任意创建或控制其他
窗口的权限。

托盘绑定所属窗口生命周期，要保留托盘可隐藏该窗口，显式 `quit()` 结束进程。
图标采用平台原生应用/状态图标，不提供统一自定义图像格式。Linux 当前使用
GTK3 XEmbed 托盘，必须在 1500 ms 内确认宿主嵌入，否则 `E_TRAY_UNAVAILABLE`；
Wayland-only/无兼容托盘宿主不保证可用，后续宿主消失也会报告失败。
能力标志表示后端实现，不保证当前桌面会话有可用托盘或已授予焦点。

## 公共行为与失败

- `initialize/run/close` 保留原单窗口用法；多窗口必须通过同一应用组的
  `create_window` 创建，不能另开独立 application 组。`close` 可在回调中请求。
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
统一全局快捷键、跨平台 accessibility/Agent endpoint、自动更新、
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
多窗口/原生动作还须在目标机手动检查：

- 根窗口关闭后子窗口继续响应，最后一个窗口关闭才退出，显式 quit 关闭整个组。
- macOS 菜单 Cmd+Q 退出整组，而不是遗留其他窗口或事件循环。
- 菜单/托盘更换后，旧命令不会误投递；命令只到所属窗口。
- 目录选择等模态界面打开时退出，检查取消、关闭次序和回调不重入。
- 无兼容 XEmbed 托盘宿主时明确失败；宿主重启/消失不保留假成功状态。

完整本轮结果见 [2026-10-20 实际记录](acceptance/unified-desktop-2026-10-20.md)。
具体检查清单见 [Linux](../tests/portable/desktop-linux.md)、
[macOS](../tests/desktop/macos-wkwebview.md)、
[ADR-0119](adr/0119-unified-system-webview-desktop.md)、
[多窗口与原生动作决策](adr/0120-managed-desktop-windows-and-native-actions.md)、
[开发包决策](adr/0121-unified-desktop-development-package.md)。
