# macOS WKWebView 后端验证清单

本文是目标机待执行清单，不是通过记录。2026-10-20 编写环境为 Linux，没有
Apple SDK、AppKit、WKWebView 或 macOS 桌面；没有执行本文件的原生编译、链接、
窗口、权限、输入或生命周期测试。共用协议测试和其他平台窗口测试不能替代这些项目。

## 构建前提与范围

- 本新增桌面路径要求 macOS 12.0+、相应 Apple SDK、Objective-C++17 和 ARC。
  原因是通过公开 `WKUIDelegate` 接口明确拒绝摄像头与麦克风请求。此下限不改变
  旧 Windows 路径或无窗口 Runtime 的系统要求。
- 链接 `AppKit.framework`、`CoreFoundation.framework`、`Foundation.framework`、
  `WebKit.framework`。调度使用主 run loop 的 default、modal 和 event-tracking
  modes，不引入额外第三方二进制。
- 源码目标是 `x86_64`、`arm64` 和同时包含二者的 `universal2`。现代 macOS
  的 32 位桌面不在范围内；编译出某个 slice 不代表在对应硬件运行通过。
- `application` 必须在主线程创建、调用和销毁；仅 `dispatch` 可从后台线程提交。
  应用对象必须活过全部提交者。该后端独占单个 `NSApplication` 的运行循环，
  已有运行循环或已有 application delegate 时返回 `E_APPLICATION_IN_USE`。
- 使用临时、不持久化的 `WKWebsiteDataStore`。非空 `user_data_directory`
  返回 `E_UNSUPPORTED`，不会静默忽略或把多个应用接到共享持久化 profile。

目标 Mac 可先从仓库根目录执行以下独立翻译单元检查；完整 CMake 构建、链接和
安装后消费者仍需另验，不能以此代替：

```sh
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/soui-wkwebview.XXXXXX")"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
for arch in x86_64 arm64; do
  xcrun --sdk macosx clang++ -std=c++17 -fobjc-arc -Wall -Wextra -Wpedantic \
    -isysroot "$sdk_path" -mmacosx-version-min=12.0 -arch "$arch" \
    -I sdk/include -c sdk/src/desktop/backends/macos_wkwebview.mm \
    -o "$build_dir/wkwebview-$arch.o" || exit 1
done
xcrun lipo -create "$build_dir/wkwebview-x86_64.o" "$build_dir/wkwebview-arm64.o" \
  -output "$build_dir/wkwebview-universal2.o"
xcrun lipo -info "$build_dir/wkwebview-universal2.o"
```

记录 Xcode/Apple Clang 版本、SDK 版本、系统版本、机器架构、配置、完整命令、
退出码和当前源码提交。`lipo` 结果仅证明对象包含两个架构。

## 目标机行为与失败路径

1. 启动与基础功能：实际显示本地入口，验证 document-start 时已存在 Bridge；
   原生请求与 JSON 响应、原生事件、中文和 emoji 往返。验证标题、内容区尺寸、
   隐藏/显示、最小化后 focus、键盘复制粘贴、中文输入法、Retina/多屏和系统缩放。
2. Bridge 来源：字符串消息只能由注册 WebView 的真实顶层 frame 送入共用解析器。
   直接发送对象、错误 handler 名、伪造 token、旧导航 token、超大字符串、含 NUL
   的字符串或错误版本不得触发业务方法。子 frame、空来源 URL 和根外 URL 均拒绝。
3. 导航与读权限：入口和所有原生导航仍受共用 `allow_navigation` 策略校验。
   `location.reload()`、同 URI 的 `location.assign()`、同 URI 链接和 back/forward
   的完整文档请求须先取消，再经共用 reload 生成新 token、重置 request ID、
   重新触发 ready。测试连续原生导航和 renderer 导航竞态，确保旧 WKNavigation
   不能提交。另测指向 `#fragment` 的 meta refresh；如引擎将其视为完整导航，
   必须在 response 策略阶段拒绝，再创建新会话。
   真正的同文档 fragment 改变可用，网络 URL、根外文件、`javascript:`、`data:`、`blob:`、
   `window.open`、`target=_blank` 和下载链接均不得产生新顶层页面/窗口/下载。
   本后端保守禁止子 frame 文档导航；不将 iframe 支持描述为已实现。
   本地 CSS/图片/脚本仅获得配置资源根的读授权。目录选择不得扩大此授权。
   另测资源根内指向根外的符号链接，记录真实 WebKit 结果。
4. 设备与网页 UI：实际尝试摄像头、麦克风及组合请求，必须被拒绝。
   网页文件上传 picker 返回取消。`alert` 直接完成，`confirm` 返回 false，
   `prompt` 返回 null；不提供隐式阻塞对话框。上述策略不等于完整网络访问沙箱，
   页面子资源和 OS 隐私行为仍需按产品配置单独评审。
5. 剪贴板：实际系统剪贴板和独立应用之间验证双向 Unicode 文本、空文本；
   无文本格式时返回 `E_CLIPBOARD_FORMAT`。读取不得超过 1 MiB，拒绝 NUL
   和不能无损转换为 UTF-8 的文本；写入失败必须返回错误。
6. 目录选择：验证成功、取消、目录消失、中文路径、符号链接/alias 和重入
   `E_BUSY`。仅返回目录路径，不自动授予网页读取或上传能力。
7. 关闭与异步：窗口关闭按钮、Cmd-W、Cmd-Q、native close、页面 close 均必须
   恰好一次通知 closed 并使 run 返回。close 前提交的 dispatch 在关闭后不得执行；
   另从 AppKit 直接发送 `NSWindow.close`，用独立观察者统计 will-close 通知，
   应只有一次，不能从 delegate 再次 close。在该通知和清理同步回调中再次请求
   backend close，确认清理不重入，ARC 保持窗口至外层 close/performClose 返回，
   委托至少存活到当前委托调用结束；
   重复 close/析构仍只通知一次，不得重新 create 已关闭的私有 backend。
   late JavaScript completion、旧导航完成/失败不得访问旧宿主或污染新导航。
   dispatch 队列在 1024 个待执行工作处拒绝额外提交；普通 native work 不因导航
   被丢弃。脚本消息独立限制为 64 KiB/条、1024 条排队，脱离 WK 委托栈异步交付。
8. 模态关闭：私有 backend 专项测试中，目录 picker 的嵌套 modal loop 从后台
   通过 backend dispatch 请求 close；
   picker 必须退出，choose_directory 返回 `E_CLOSED`，外层 run 也须退出。
   必须包含由 backend dispatch 工作自身打开 picker、第二个后台 dispatch 请求 close
   的场景，以及在 run/第一次 drain 前已经连续排入 picker 和 close 的场景，
   防止串行 main queue 或 run-loop 批处理的模态重入死锁。
   公共 `application::dispatch` 则遵循业务回调串行契约：若第一项工作同步打开
   picker，第二项工作须等 picker 返回，不能用它要求中断第一项。验证取消
   picker 后第二项才执行，且回调没有重入；另测原生窗口关闭/应用退出路径
   在模态期间的取消与延迟 closed 通知，不将私有 backend 能力等同公共取消 API。
   验证在 ready/error/业务回调中调用 close，以及启动后立刻关闭和未 run 前关闭。
   禁止从回调里销毁 application 对象，此限制不因 shared state 而改变。
9. 进程与重复生命周期：终止 WebKit 内容进程后应收到 `E_WEB_PROCESS` 并关闭，
   不自动使用旧 token reload。反复创建/导航/关闭/销毁，检查 Instruments、
   Zombies 和 Address Sanitizer，观察窗口、委托、WebKit 子进程与内存是否回收。
10. 配置失败：低于 12.0 的部署目标、缺 ARC、非空自定义 profile、非主线程
    调用、无效 UTF-8、失效资源根、无效尺寸和重复创建均应得到明确失败。

## API 核对来源

- [WKWebView 本地文件读取范围](https://developer.apple.com/documentation/webkit/wkwebview/loadfileurl(_:allowingreadaccessto:))
- [WKScriptMessage 的真实来源 frame](https://developer.apple.com/documentation/webkit/wkscriptmessage/frameinfo)
- [WKUIDelegate 摄像头与麦克风决策](https://developer.apple.com/documentation/webkit/wkuidelegate/webview(_:requestmediacapturepermissionfor:initiatedbyframe:type:decisionhandler:))
- [WKUIDelegate 文件上传回调](https://developer.apple.com/documentation/webkit/wkuidelegate/webview(_:runopenpanelwith:initiatedbyframe:completionhandler:))
- [WKDownload 取消](https://developer.apple.com/documentation/webkit/wkdownload/cancel(_:))
- [NSApplication stop 与事件循环唤醒](https://developer.apple.com/documentation/appkit/nsapplication/stop(_:))
- [NSWindow close 与关闭通知](https://developer.apple.com/documentation/appkit/nswindow/close())
- [ARC 所有权与精确生命周期](https://clang.llvm.org/docs/AutomaticReferenceCounting.html)
- [CFRunLoopPerformBlock 跨线程调度](https://developer.apple.com/documentation/corefoundation/cfrunloopperformblock(_:_:_:))
