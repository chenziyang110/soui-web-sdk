# ADR-0138：可取消的用户关闭与整组退出

状态：代码实现，macOS DOM 关闭否决的限定原生探针已通过；其他原生关闭交互仍待验收。程序 close()/quit() 保持强制兼容语义。

新增 request_close()/request_quit() 与同步 close_requested(window_id)->bool。
未提供回调默认允许；false或异常拒绝，异常另报可控错误，不在原生回调等待网页Promise。
返回成功仅表示请求被接纳处理，当前已有业务回调时可延后到公共回调队列；不是窗口
一定关闭的证明。审批期间递归/重复请求拒绝busy，不重复弹确认或无限递归。

原生标题栏/菜单关闭请求只通知公共层；批准后才调用后端close，避免native delegate
递归再次询问。系统应用退出先预检当前所有窗口；任一拒绝/异常/窗口集合变化都取消
本轮统一退出，不提前关闭已同意窗口。业务自己显式forceclose属于应用授权行为。

异步确认须返回false取消当前请求，保存或确认后由业务显式close/quit；再次request
会再次询问。关闭发生在UI线程；关闭动作沿用既有导航、pending任务和晚到回调失效屏障。
已接纳的关闭请求属于原生窗口，不绑定某一网页代际，导航不会撤销它；审批时读取
宿主当前业务状态。网页invoke本身仍受既有来源/令牌限制。
没有自动网页强制关闭能力、没有关闭OS安全保护；示例通过现有显式业务invoke演示
未保存保护。业务保护状态应随所属文档生命周期更新，不继承旧页面授权。

本轮不拦截OS强制进程终止，不保证系统关机可撤销，也不新增自动持久化、保存对话框
或异步JS权限协议。聚焦共用层审批/重入/窗口变化回归，真实系统关闭行为另记证据。

## macOS DOM 关闭通知也须审批

WKUIDelegate 的 `webViewDidClose:` 可能由初始主文档的 `window.close()` 触发，
不能将该通知当成宿主授权。此前直接 shutdown 会绕过 close_requested；现在与
标题栏关闭共用有界请求分派，保持状态及回调副本存活，由公共审批决定是否销毁
原生窗口。失效 sender、已关闭状态、无回调或异常都不会隐式强制关闭。程序主动
close/quit 的既有强制语义与 WebKit 进程终止清理不变。

拒绝只保护宿主窗口与关闭审批，不承诺回滚 WebKit 已进行的 DOM closing 状态或
恢复网页内部执行。官方实现链路见
[DOMWindow::close](https://github.com/WebKit/WebKit/blob/main/Source/WebCore/page/DOMWindow.cpp) 和
[WK Cocoa UI delegate](https://github.com/WebKit/WebKit/blob/main/Source/WebKit/UIProcess/Cocoa/UIDelegate.mm)。
生产私有分派器的小型 C++ 检查覆盖否决、同意后迟到重复、失效 sender、空回调、
异常及审批过程中释放外部 owner；不将它计为 WKWebView 的原生 window.close 实测。

### 2026-10-24 macOS 限定原生验证

基于本地修复快照 `4ecaf7fa7221fa564c82e886fced8487e8e3f823`，独立 Mac
执行记录在 08:56 UTC 完成。后端与私有关闭分派头的两个源码 blob 均匹配；
补丁检查、应用、差异检查，以及独立 SDK configure/build/install、消费者
configure/build/stage/verify 均返回 0。两份 SDK 库及消费者可执行文件包含
arm64、x86_64 两个 slice；真实探针在 Apple Silicon 主机执行，未验收 Intel 运行。

产物为 `out/dom-close-veto-staged/DomCloseVetoConsumer.app`，bundle ID 为
`org.soui.domclosevetoconsumer`。staging 清单包含 12 个文件、1,449,946 bytes，
`nativeRuntimeVerified` 仍为 `false`，不因独立专项运行而改写通用交付清单字段。

临时消费者通过业务 Bridge 就绪信号后仅调用一次 DOM `window.close()`；宿主
`close_requested` 返回 false，再在后续派发中确认原生窗口仍存在、窗口状态可读，
最后使用公开程序 `close()` 清理。专属报告为：

```text
PASS armed=1 approvals=1 preserved=1 closes=1 errors=0 timeout=0
```

正常 CUA 启动调用返回 `AXError.cannotComplete`，因此不把工具返回计为视觉成功，
也没有本轮截图证据。专项报告证明上述审批及清理链路完成，随后应用清单显示
`isRunning=false`、`useCount=1`。本结果不证明网页内部 DOM 状态可恢复，不覆盖
其他关闭入口、全部桌面能力或其他平台；已有小型 C++ 检查与本次原生探针分开计证。
