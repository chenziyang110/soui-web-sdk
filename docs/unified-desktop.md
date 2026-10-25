# 统一桌面入口：同一份 C++ 与 Web 代码

`Soui::Desktop` 提供独立于 SOUI/Miniblink 的受管理多窗口应用入口。相同
`soui::desktop::application` API 和本地 HTML/CSS/JavaScript，可选择
GTK3/WebKitGTK、AppKit/WKWebView 或 Win32/WebView2 后端。业务代码不需要平台
`#ifdef`。示例位于 [examples/unified-desktop](../examples/unified-desktop)。

这是未发布开发功能，不是 Electron 全功能替代或“全部架构完美运行”声明。
Windows x64 Release 已完成 MSVC 原生编译、安装 SDK 与独立消费者交付，真实 WebView2
探针的 Bridge/模块/同源 fetch/公开关闭通过，未做视觉人工交互，见[限定记录](acceptance/unified-desktop-2026-10-23.md)。
macOS 已完成限定 universal2 原生编译及安装SDK交付链，
2026-10-24 新候选在 ARM Mac 正常 CUA 启动后完成最小窗口、Bridge、模块/同源 fetch
与正常关闭验证，见[新候选记录](adr/0145-observed-macos-activation-policy.md)。
旧 DeliveryConsumerFinal 的 Finder 无窗口及 SouiMacVerify 激活失败仍属各自旧包证据，
不能用新包成功改写历史；完整六纵切原生交互和 Intel slice 执行仍未验收。
Linux于2026-10-22完成限定真实GUI、Bridge和模块
验证，见[记录](linux-native-validation-20261022.md)。后续环境快照丢失该自构建
运行时，新增部署RPATH策略尚未重新GUI验收，不能推广为干净目标机部署通过。
正式分发前必须完成目标机验收与版本、许可、交付清单更新。

整体目标、三阶段代码主线与收口门槛见[跨平台迭代路线](cross-platform-roadmap.md)。

## 源码路径、依赖与本轮证据

| 平台 | 桌面目标 | 后端与最低前提 | 本轮证据 |
|---|---|---|---|
| Linux | x64、ARM64 | GTK 3.24+、WebKitGTK 4.1 2.40+、C++17、正常图形会话 | Debian 13 x64 / GCC 14.2 / GTK 3.24.49 / WebKitGTK 2.52.6 编译链接、安装消费者通过；限定x64 GUI/Bridge/模块链已验证；后续部署策略未复验，ARM64未编译 |
| Linux | x86、ARM32 | 仅当目标发行版提供上述架构的完整开发/运行包；需正确 ISA/浮点 ABI/sysroot | 未编译或 GUI 验证，不承诺所有 32 位发行版可用 |
| macOS | x64、ARM64、universal2 | macOS 12+，Apple SDK，Objective-C++17/ARC，AppKit、Foundation、WebKit、CoreFoundation | AppleClang 21 原生 universal2 编译/链接、安装SDK消费者与交付通过；ARM宿主fake契约通过；2026-10-24新候选限定真实窗口/Bridge/模块/fetch/关闭通过；完整原生交互及Intel slice执行未验收 |
| Windows | x86、x64、ARM64 | MSVC-compatible C++17、Windows SDK、匹配架构 WebView2 SDK loader、当前受支持 Evergreen Runtime | x64 Release / MSVC 19.44 原生编译、安装 SDK 与独立消费者交付通过；真实 WebView2 Bridge/模块/fetch/公开关闭探针通过，无视觉人工交互；x86、ARM64 未编译运行 |
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

`popup_menu(vector<menu_item>)` 显示层级原生弹出菜单，`set_tray(tray_options)`
设置窗口所属托盘，`clear_tray()` 移除。命令通过该窗口的 `callbacks.command`
回调交付，`callbacks.lifecycle` 提供带窗口 ID 的 ready/closed 事件。菜单最多
64 个节点（父项、命令和分隔符均计数），根节点深度 1、最多 4 层；空根列表合法且
不派发命令。尾部 `children` 非空表示子菜单，父项必须有标题、空 id、checked=false。
分隔符 id/label/children 必须为空；叶命令 ID 在整棵树中唯一有界、文本必须有效 UTF-8。
禁用父项同时禁用全部后代，公共层也阻止其命令派发；替换菜单后旧 revision 回调失效。
popup 与 tray 共享模型；菜单栏和应用内 accelerator 仍不在本轮契约内。这些接口仅供原生业务显式调用，不给网页任意创建或控制其他
窗口的权限。

托盘绑定所属窗口生命周期，要保留托盘可隐藏该窗口，显式 `quit()` 结束进程。
图标采用平台原生应用/状态图标，不提供统一自定义图像格式。Linux 当前使用
GTK3 XEmbed 托盘，必须在 1500 ms 内确认宿主嵌入，否则 `E_TRAY_UNAVAILABLE`；
Wayland-only/无兼容托盘宿主不保证可用，后续宿主消失也会报告失败。
能力标志表示后端实现，不保证当前桌面会话有可用托盘或已授予焦点。

## 打开与保存路径选择（不读写文件）

所选值是本平台文件系统绝对路径而非 URL；网络挂载或 UNC 路径不代表物理本机存储。

`choose_files(open_file_options)` 返回 `file_selection_result{status, paths}`；
`choose_save_file(save_file_options)` 返回 `value_result{status, value}`。原有目录选择不变。
原生取消为 `E_CANCELLED`；拥有窗口关闭、编码错误或原生失败不转成取消。

参数 title 最多 4096 UTF-8 字节；extensions 最多 16 个、每个 1..16 ASCII 字母数字，
小写去重，空集合不限类型。它只给原生选择 UI 提示，不是内容校验或安全白名单；
不支持 MIME、任意 glob 或过滤器分组。打开默认单选，multiple=true 时最多 32 个路径；
保存只有一个。返回路径每项最多 32768 UTF-8 字节、总计最多 128 KiB，使用实际平台
filesystem 校验绝对路径，不接受 NUL/无效 UTF-8，不截断超限返回。

保存 default_name 是最多 255 字节的便携 basename 建议，可留空。拒绝路径分隔、控制字符、
Windows 保留名/字符及末尾空格或点；不提供任意默认目录。原生面板可能调整扩展名，
返回用户最后确认的路径，而不是 SDK 拼造的文件名。

网页通过 `services.file_open_dialog` 与 `services.file_save_dialog` 两个独立默认 false
开关授权 `desktop.dialog.openFiles` / `desktop.dialog.saveFile`；前端 `dialogs.openFiles()` /
`dialogs.saveFile()` 提供 typed 结果。只有明确取消解析为 cancelled=true 及空数组/空路径；
其他错误拒绝 Promise。授权属于宿主信任策略，不是系统验证的网页手势。

模态操作持有窗口状态，共用目录/文件选择的忙碌与关闭槽。关闭结束原生面板，导航后
旧请求不能把路径结果发给新文档；网页请求超时本身不表示系统面板已取消。

保存只是选择目标路径，不表示文件已经保存。SDK 不读取、创建、截断或写入所选文件，
不生成文件访问令牌、持久授权或安全书签，也不扩大资源根目录/WebView 文件读取范围。
保留原生覆盖确认，GTK/Mac 面板禁建目录，Windows 禁保存前测试文件创建；系统自身行为
和用户在系统界面的动作仍由对应系统处理。示例按钮仅显示路径，没有文件读写操作。

## 原生宿主全局快捷键

`register_global_shortcut(id, global_shortcut{key, modifiers})` 与
`unregister_global_shortcut(id)` 只供可信 C++ 宿主使用。`callbacks.global_shortcut(window_id, id)`
与菜单命令分开，不自动发网页事件、不证明用户手势；网页没有标准注册方法或默认权限。
示例只允许用户点击注册一个固定的 Primary+Shift+F9“显示此窗”业务绑定，不在启动时注册。

键枚举限 F1..F11、方向键、Home/End/PageUp/PageDown/Insert/Delete；F12、字符键、
数字键及媒体键不接受。`control/alt/shift/primary` 为位集合，primary 在 Mac 为 Command，
其他平台为 Control；至少包含 control/alt/primary，Shift 单独不足。Mac Insert 不支持，
Delete 指 Forward Delete 而不是退格；系统 Fn 设置、占用和布局差异不保证相同物理按法。

每窗最多 32 项，含注销失败待清理项。同 ID 或规范化组合重复注册会失败，不隐式替换；
未知 ID 注销幂等成功。注销立即禁回调，即使 OS 清理失败；失败记录保留供同 ID 重试。
若注册失败后的回滚也失败，`shortcut_cleanup_pending` 同样保留逻辑 ID/配额，需按原 ID 注销重试。
注册身份不复用，旧事件不能触发重注册后的绑定；有限原生 ID 池耗尽会明确失败，不能回绕。
Mac 进程级 router 另限制 1024 项（含保留清理项）。关闭先撤销回调，再尝试全部原生清理，
最终析构是 best-effort，不把错误描述为确认释放。宿主注册跨导航保持，到显式注销或窗口关闭
结束；长期按住可能依平台重复，SDK不保证“一次物理按键恰好一次回调”。

Windows 使用 RegisterHotKey；Mac 使用 Carbon EventHotKey，不添加全局键盘监听或
Accessibility 授权。Linux 用可选 X11 实现，每个注册独立拥有显示连接，既不复用旧事件身份，
也不排空主 GTK 事件队列。仅 `XDG_SESSION_TYPE=x11` 且未设置 `WAYLAND_DISPLAY` 的确认 X11 会话可尝试；Wayland（含 XWayland）及未知会话
返回 not_supported，不使用模拟按键作为替代。X11 键位/修饰映射有歧义也拒绝；
映射变化会使原注册失效，需要显式注销并重新注册。Portal 注册/用户重绑定是后续独立能力。

构建默认探测 `gdk-x11-3.0`/`x11`，缺失仍能构建但全局快捷键不可用；
`-DSOUI_DESKTOP_ENABLE_X11_SHORTCUTS=OFF` 可明确禁用。启用依赖会传播至安装 SDK
消费者，不能把有 X11 静态依赖的包说成无需 X11。`global_shortcuts` capability 表示后端
实现可尝试，不表示组合键一定能注册。注册错误不全部归为占用冲突。

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
  若当前回调同步等待目录选择，后续 `dispatch`（包括调用 `close` 的工作）
  也须等该回调返回；`dispatch` 不是中断当前模态操作的取消接口。
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
不要加载不受信任脚本；示例 CSP 禁止 iframe、对象与跨源连接，仅允许同源 fetch。

主 HTML 根边界检查不是完整文件系统沙箱；它不证明所有脚本、CSS、图片资源
均在根内，也不消除资源目录被其他进程改动的 TOCTOU。保护资源目录完整性、
限制业务方法权限是应用责任。既有 Runtime 的本地 IPC 身份认证要求不改变。

GTK 保守拒绝无原生 ticket 的同 URI `location.assign` 等完整导航，明确
renderer reload 则转公共重载；应用主动导航应走原生 API。只有引擎确认为真正
同文档的 fragment 更新保留当前会话。WebKitGTK 的 file-origin 防护保持开启，
任意本地 fetch/ES module 或 Vite 绝对资源路径不保证与 Electron 相同；需使用
适合本地页面的资源输出，或显式启用下述本地 HTTP 资源源（真实引擎验证待完成）。

字体、CSS、媒体编解码、焦点、输入法、DPI 和渲染时序随系统引擎变化。当前尚无
统一全局快捷键、跨平台 accessibility/Agent endpoint、自动更新、
安装签名或完整 Electron API 兼容。macOS 默认非持久 WebKit 会话且不支持任意
`user_data_directory`（明确报错）；Windows/Linux 的数据目录策略见后端说明。

## 显式本地资产读取

`file://` 上的原生 fetch、ES module 和 CORS 在系统引擎之间并不等价。
配置 `options.enable_asset_read = true` 后，同一份前端可以调用
`await window.souiDesktop.readAsset('config.json')`；默认关闭，按窗口单独授权。
返回 `{mime, size, base64}`：size 是原始字节数，base64 是原始字节的标准编码，
MIME 按扩展名推断，不做内容识别，也不声称文本必为 UTF-8。调用方应显式解码，
例如示例使用 `atob` → `Uint8Array` → 严格 UTF-8 `TextDecoder` → `JSON.parse`。

每文件至多 256 KiB，base64 至多 349,528 字节，仍受现有 1 MiB Bridge 响应上限
约束。读取在进程共享的两个工作线程执行，最多16个未完成作业（含等待 UI
交付的结果），饱和时返回 `asset_queue_full`。仅适合小型本地配置/资产，
不适合流媒体或网络盘；大小限制不保证操作系统 I/O 延迟。资源树必须由应用拥有且运行期间不被不可信
进程修改，规范化边界检查不是完整文件系统沙箱，也不消除恶意并发替换竞态。

path 是至多 4096 UTF-8 字节的根相对路径，不是 URL。拒绝空段、`.`/`..`、
绝对路径、反斜杠、冒号、百分号、query/fragment、控制字符，根外 symlink、
目录和非普通文件。目录选择不扩大授权。内置 `soui.asset.read` 保留方法名
不能作为业务 handler 使用；未启用时返回 `asset_read_disabled`，非法参数返回
`invalid_asset_path`，其余文件错误通过 Promise 错误 code 返回。

读取使用原有顶层 Bridge、随机令牌、请求 ID 与导航代际，拒绝远程导航上下文、
可识别的子 frame、旧会话和其他窗口的令牌。GTK 没有原生 frame 来源证明，
仍依赖顶层 preload 闭包令牌；同源脚本可调用父页公开接口，不是独立安全主体。
既有 `invoke` 不变；此 API 不拦截 fetch、
不提供 HTTP/Range、自定义 origin、原生 ES module 或网络能力。详见
[ADR-0123](adr/0123-opt-in-local-asset-reader.md)。示例的“Read local JSON”按钮
展示实际消费路径，不能以 VM 或 fake-backend 测试代替真实 WebView 验收。

导航、关窗和析构撤销旧会话：未开始的作业被移除，运行中或已排队到 UI 的
旧结果被丢弃，不向新页面回调。原有 pagehide 拒绝页面待处理 Promise；并不新增
对已卸载页面的取消消息。取消/Promise 超时不能硬中断已开始的系统文件读取，
它继续占用工作线程和额度直到返回；关闭不等待其结束，也不增加替代线程。
若两个读取长期阻塞，后续请求可能饱和，但 UI 不为资产 I/O 等待。
完成结果按现有业务回调串行规则交付，不在工作线程或模态重入中调用业务代码。
若原生投递队列已满或关闭而拒绝完成消息，结果释放，Promise 由原有30秒超时
或 pagehide 拒绝收束；不会无限重试或跨线程强行交付。
详见 [ADR-0124](adr/0124-asynchronous-asset-read-lifetime.md)。

## 显式本地 HTTP 资源源

设置 `options.enable_resource_server = true`，原生选定的 HTML 改由窗口专属的
`http://127.0.0.1:<随机端口>/<随机前缀>/` 加载。默认关闭，旧 file URL 和
`readAsset` 调用均不变；此选项不隐式开启 `readAsset`。三个后端已接入同一
服务与导航授权，目的是支持标准相对 CSS/图片、ES module、动态 import 和
fetch，不宣称这些功能已在三引擎实测通过，也不是 Electron API 兼容层。

示例运行 `soui_desktop_example <资源目录> --resource-server`，点击
“Load modules and fetch JSON” 执行动态模块 → 静态依赖 → 同源 JSON fetch。
前端构建应使用相对 base（例如 `./`）；绝对 `/assets/...` 会跳出随机前缀并
返回错误，不自动改写 HTML、模块或 URL。仅 GET/HEAD，query 不参与文件定位，
无 SPA fallback、Range、目录列表、重定向、上传或代理。MIME 按扩展名映射，
`.js/.mjs` 为 `text/javascript`，未知类型为 `application/octet-stream`；
响应为原始字节，不做转码，应用应提供 UTF-8 文本与正确 HTML charset。

每窗口一个后台服务，进程最多16个存活服务；已停止但系统读取未返回的服务
继续占额度。每文件最多8 MiB、请求头最多16 KiB，每次网络接收/发送阶段有
2秒截止时间，每服务串行处理连接，无 keep-alive。读文件在后台，关闭仅撤销
服务、不等待 I/O，无法硬取消系统读取；资源根/入口的既有初始化校验仍有
同步文件元数据操作，因此不承诺任意网络盘上的启动或导航无阻塞。

仅绑定 IPv4 loopback，要求精确 Host 和系统熵随机前缀，拒绝路径穿越、非法
编码、根外 symlink 和非普通文件。响应使用 no-store、nosniff、no-referrer，
不设置 CORS 放宽。资源树必须可信、由应用拥有且保持不可变；不是完整文件系统
沙箱，不消除并发替换竞态。随机前缀不防同用户恶意进程，不应分享资源 URL。
不同窗口端口隔离 origin；同一窗口内路径和端口稳定，重启后改变，不承诺跨重启
存储身份。此 HTTP 地址不是 HTTPS，不承诺所有安全上下文 API 或系统代理/
防火墙策略下均可用。监听失败明确报错，不降级绕过安全策略。

根内资源可供该页面读取，不代表根内其他页面获得 Bridge。顶层文档仍由原生
显式选择，来源、每导航令牌、代际和子 frame 限制保持原有契约；服务不会开放
远程导航。应用仍需 CSP 约束脚本和子资源网络，不能把同源不可信脚本当作隔离
主体。详见 [ADR-0125](adr/0125-opt-in-loopback-resource-origin.md)。

本增量只做必要构建、语法与高风险边界检查，完整统一回归延后：
- 三引擎完整模块、相对资源、CSP 与目标环境矩阵；Mac 新候选及 Windows x64 已有下述限定原生链路证据，不覆盖完整兼容性。
- 真实 HTTP Host/方法/编码/大小/慢连接及窗口关闭、导航、进行中请求的竞争。
- 多窗口源隔离、存储行为、目标机网络策略，以及各架构和安装包回归。
这些待验收项不能由下方早期基线测试结果替代。

## 实际验证和待验收

本轮成功：Linux x64 真实头库编译/链接、独立安装包消费者构建、公共
application/protocol 的 fake-backend 契约测试、真实 preload 的 Node VM 测试。
fake 和 VM 不构成真实浏览器或窗口验收。

以下为2026-10-20的历史阻塞，Linux后续限定通过见[2026-10-22记录](linux-native-validation-20261022.md)，
不代表新增部署策略已复验。最初无显示服务时原生测试返回 `E_DISPLAY`，不能创建
Xvfb/D-Bus 所需 Unix socket，提升入口也未能初始化。后续在受支持的既有云
图形桌面启动样例，因系统前缀缺少 `WebKitNetworkProcess` 而在加载页面前中止。
当时需要正确安装的系统 WebKitGTK 运行依赖，不能只提供隔离的编译头库。
没有修改权限或关闭 WebKit sandbox；两次启动均不是通过或 skip。

在已有正常图形会话和依赖的 Linux 目标机：

```sh
cmake --preset desktop -DSOUI_DESKTOP_GUI_TESTS=ON
cmake --build --preset desktop
ctest --preset desktop -R sdk.desktop.linux-native --output-on-failure
```

原生测试有真实页面/JavaScript/Bridge、剪贴板、目录取消、线程调度、重载和
关闭用例，缺少显示服务时必须失败。Windows x64 编译及限定 WebView2 探针、Mac 新候选最小窗口与 DOM 关闭否决已有独立证据；不能据此视为这些完整原生交互均已验收。
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

### 开发包失败关闭补充

入口在构建后重新核对缓存和 CTest JSON 注册清单，九项必需测试（含真实文件资产边界及有界工作线程）须唯一、启用并具有执行命令。多配置生成器仅接受已配置的目标；安装版本须匹配源码 VERSION，且必须包含选中配置的 Runtime/ Desktop 静态库。它不以其他测试通过、旧缓存或其他配置文件替代这些条件。

应用二进制与前端资源的可搬移目录入口见 [应用 staging](desktop-application-staging.md)，
与上述 SDK 开发 ZIP 分开；不自动提供系统引擎、签名或正式安装器。

需要新建应用可使用 [最小项目生成器](unified-desktop-project-generator.md)，
直接接入已安装 SDK、前端模块和应用 staging，不复制演示业务。

### Linux 显式 helper 部署预检

官方发行 WebKitGTK 的 helper 位置是运行库构建时的 PKGLIBEXECDIR；开发包 .pc
不一定提供它。仅解包共享库到用户目录可能可以链接，但会在创建 WebView 时因
固定前缀缺少 WebKitNetworkProcess 中止进程。WEBKIT_EXEC_PATH 并非发行版通用
覆盖项，不要通过改二进制或关闭沙箱解决。

已知准确部署位置时，构建 SDK 可传入
`-DSOUI_DESKTOP_WEBKIT_HELPER_DIR=/actual/target/webkit-helper-directory`。这是由
部署方确认的目标系统路径，不是开发机 sysroot 路径，也不是 helper 搜索路径
覆盖。SDK 在 GTK/WebKit 初始化前检查 WebKitNetworkProcess、WebKitWebProcess
是否为普通可执行文件；缺失时 initialize 返回 E_WEBKIT_RUNTIME，消息不包含
绝对路径。符号链接可指向正常安装文件，不会执行 helper 或修改权限。

默认空值表示未知，保持现有行为，**不承诺默认配置可防止 WebKit abort**。
候选存在不代表加载器、架构、辅助依赖或沙箱可运行；文件检查也无法排除并发
修改。该契约编入静态 SDK，安装后的消费者无需重复传入，但运行库部署前缀变化
必须重新构建 SDK。独立直接编译 backend、没有生成配置头时亦保持未知默认。
已有 doctor 的标准位置候选仍是 UNKNOWN/PASS 候选提示，不被升级为部署事实。

依据：[WebKit 2.52.6 helper 位置实现](https://raw.githubusercontent.com/WebKit/WebKit/webkitgtk-2.52.6/Source/WebKit/Shared/glib/ProcessExecutablePathGLib.cpp)
和 [GTK 构建安装目录定义](https://raw.githubusercontent.com/WebKit/WebKit/webkitgtk-2.52.6/Source/cmake/OptionsGTK.cmake)。

## 窗口状态控制（显式网页授权）

统一 C++ 接口 `minimize()`、`maximize()`、`restore()` 与 `get_window_state()`
不要求业务选择平台 API。请求返回成功仅表示已提交；窗口管理器可能延迟或拒绝
最终效果。`window_state_result.status` 成功时才可读观测值，未知/未实现窗口返回
错误，不能把默认 `false,false` 当作 normal。`minimized` 与 `maximized` 独立，
系统可保留最小化前的最大化状态；macOS 使用窗口 zoom，不是全屏。
`callbacks.state_changed(window_id, window_state)` 报告原生观测变化，可合并连续状态。
能力字段 `window_minimize/window_maximize/window_restore/window_state_observation`
表明后端接口可用，不保证当前桌面管理器立即执行请求。

网页默认没有这些权限。宿主设置 `options.enable_window_controls=true` 后才注册
当前窗口保留服务 `desktop.window.minimize/maximize/restore/getState`（参数为空对象）。
前三者返回 `{accepted:true}`，查询与 `desktop.window.stateChanged` 返回
`{minimized,maximized}`。仅这四个固定方法名保留，其他 desktop.window.* 仍走业务 invoke；使用固定名称
的旧业务应重命名或迁移到显式授权服务。不能通过传入窗口 ID
控制其他窗口。沿用现有来源、令牌、请求上限、pagehide 和导航代际失效规则。
原生窗口回调与页面事件分开：旧文档事件不会在新页面接收，关闭后不再发出。

统一示例显式授权，展示按钮与观测状态；前端 SDK 提供 `windowControls` 封装。
本增量不是完整 Electron API 等价，不包含全屏、无边框拖动或可取消关闭请求。
详见 [ADR-0137](adr/0137-observed-window-state-controls.md)。

## 用户关闭审批与未保存业务

`request_close()` 和 `request_quit()` 请求 `callbacks.close_requested(window_id)`
同步审批，未提供回调时默认允许，返回 false 或抛异常时取消。回调内不应等待网页
Promise；异步确认先拒绝本次请求，保存完成或用户明确确认后由业务调用 force
`close()` / `quit()`。再次 request 会再次询问。现有程序 close/quit 行为不变。

请求可能等待正在执行的业务回调退栈，返回成功表示接纳处理而非保证已经关闭；
审批期间重复请求被拒绝，关闭后待处理请求失效。请求属于原生窗口，已接纳后不会
因网页导航撤销，审批使用宿主当前业务状态。系统应用退出先预检所有现有窗口，
任何拒绝、异常或窗口集合改变都取消整组操作，不先关闭已同意的那一部分。
应用自己在审批回调调用 force close/quit 属于显式业务行为，框架不撤销它。

标题栏关闭与应用退出使用此路径；不保证可以撤销操作系统强制终止或关机。
网页不因此自动获得新的强制关闭权限。统一示例的“未保存修改”选项是每窗口独立
业务状态，通过显式 invoke 更新；文档重新就绪时重置，拒绝时发送 app.closeBlocked。
它演示保护接线，不实现自动保存。详见[ADR-0138](adr/0138-cancellable-user-close.md)。

## 统一桌面服务（默认拒绝）

宿主通过 `options.services` 独立授权 `window_properties`、`clipboard_read`、
`clipboard_write`、`directory_dialog`、`file_open_dialog`、`file_save_dialog`，
全部默认 false，与窗口状态控制开关分离。
这是应用对可信页面的预授权，不证明用户做了原生手势，不接受网页 userGesture、
窗口句柄或任意权限字段，不自动接受操作系统权限提示。只作用于当前窗口。

| 固定方法 | 精确参数 | 成功结果 |
|---|---|---|
| desktop.window.setTitle | {title:string} | {accepted:true} |
| desktop.window.resize | {width:integer,height:integer} | {accepted:true} |
| desktop.window.show | {visible:boolean} | {accepted:true} |
| desktop.window.focus | {} | {accepted:true} |
| desktop.clipboard.readText | {} | {text:string} |
| desktop.clipboard.writeText | {text:string} | {accepted:true} |
| desktop.dialog.chooseDirectory | {} | {cancelled:boolean,path:string} |
| desktop.dialog.openFiles | {title?,extensions?,multiple?} | {cancelled:boolean,paths:string[]} |
| desktop.dialog.saveFile | {title?,extensions?,defaultName?} | {cancelled:boolean,path:string} |

标题最多4096字节UTF-8，尺寸64..16384，剪贴板写入最多32KiB、读取最多64KiB，
目录路径最多64KiB；文件选择的选项、路径和取消联合类型见上文“打开与保存路径选择”。
文本拒绝NUL和非法UTF-8。原64KiB请求和1MiB响应限制仍适用。
窗口操作成功只代表请求被接纳；OS焦点策略等仍可能限制最终表现。目录取消仅将
后端E_CANCELLED映射成 `{cancelled:true,path:""}`；其他原生错误保留，所选路径
本身不是文件读写授权。原生调用与业务回调串行，导航或关闭后不发送迟到响应。

这些固定方法名保留，其他同前缀方法仍走业务invoke。示例删除的是私有演示方法，
不是旧Host公共接口；旧业务如占用固定标准名称须迁移。适配器和示例不在加载时
自动读取或写入剪贴板。模态对话框及系统剪贴板仍使用既有原生同步API；网页超时
不代表系统操作自动取消，不新增后台OS权限或硬取消承诺。
见[ADR-0139](adr/0139-explicit-desktop-services.md)。

### 资源监听启动错误诊断

`resource_server_unavailable` 仍是监听服务启动失败的稳定错误 code。
bind、listen、getsockname 系统调用失败分别在消息中标明操作和即时原生数值错误
（POSIX `errno`、Windows `WSAGetLastError`），不包含资源根、URL 或能力令牌。
数值只适用于相应系统，不能把任意失败统一归因于权限；成功调用后地址不满足
loopback 不变量时不附带旧的系统错误码。不会自动退回 file://、更换绑定地址或放宽安全设置。
生成应用启动初始化失败会向 stderr 写入错误并退出；Finder 双击时未显示窗口
本身不能区分资源解析、监听器、AppKit 或其他初始化失败，需核对同一最终包的实际诊断。


### 生成应用的可见提示与便携诊断

新生成项目对启动失败提供尽力而为的原生提示，正常启动不弹框；无显示会话或原生
提示不可用时安全保留 stderr。不改变 SDK/Bridge 权限，也不自动回退资源加载协议。
应用 `--diagnose` 输出去敏 `soui.desktop.startup/1` JSON，隐藏初始化后退出，
不安装业务回调或运行正常事件循环；不会自动写日志或上传信息。该模式仍创建隐藏
原生对象和临时监听器，成功仅表示有限初始化，不能标 nativeRuntimeVerified。
动态加载器/系统在 main 前拒绝执行时此入口不可用，仍需交付目录 doctor。
详见生成模板 README 与 [ADR0143](adr/0143-generated-startup-diagnostics.md)。
