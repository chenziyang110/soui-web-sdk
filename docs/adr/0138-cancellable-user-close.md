# ADR-0138：可取消的用户关闭与整组退出

状态：代码实现；macOS DOM 关闭否决、Windows 本地修复的首轮限定原生 fixture 已通过，Windows 最终防御修正版 fixture 编译成功但运行受阻。其他原生关闭交互仍待验收。程序 close()/quit() 保持强制兼容语义。

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

## Windows DOM 关闭通知也须进入公共审批

WebView2 的 `WindowCloseRequested` 是网页 `window.close()` 通知宿主的入口，
不是 Win32 `WM_CLOSE`。仅接标题栏消息会让 DOM 请求完全漏过 `close_requested`，
窗口仍存在也不能算宿主已否决。本地真实 WebView2 fixture 已复现审批次数为 0。

在现有事件初始化中注册该通知并检查 HRESULT，使用 weak-state、活动状态及 sender
校验。将请求放入既有有界原生派发队列，结束 COM 事件回调后再复制并调用公共关闭
请求回调，避免批准时在 WebView2 事件栈中销毁 Controller。公共层仍决定允许或否决；
无回调、失效 sender、队列拒绝或异常都不产生隐式强制关闭。队列复用既有窗口生命周期，
关闭撤销待处理工作，不新增线程、权限、接口或导航代际授权。

不直接调用 Controller.Close/DestroyWindow，不保证回滚引擎内部 DOM closing 状态，
也不承诺即时连续 `window.close()` 必有第二次通知。程序 close/quit 语义不变。
参见 [Microsoft WindowCloseRequested](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2?view=webview2-1.0.4191.47#add_windowcloserequested)。

### 2026-10-26 Windows 限定原生复验

独立本地 fixture 链接实际安装的 Soui::Desktop，未改动原模板应用。基线源码
`220aed8ce822d7f60fb50da3d7fefd7b90869954` 上，公共网页 maximize/restore/getState
和两次页面/原生状态事件通过，但一次 DOM `window.close()` 后审批次数为 0，
fixture 于 06:50:43 UTC 自然退出 1。保留该失败，窗口仍在不记为否决通过。

补入通知转发后，在独立输出重建 SDK 并构建同一 fixture，06:59:05–06:59:08 UTC
实际 WebView2 运行通过：`controlsOk=true`、`domVetoOk=true`、`vetoRequests=1`；
否决后同文档 Bridge 返回 `closed=0/windowStateLive=true`，最后公共
`request_close()` 清理触发一次允许及一次 closed，自然进程退出 0。
`errors=0/timedOut=false`，没有强杀、额外系统权限或外部 UI 注入。

此为本地修复 fixture 的原生证据，不是未修改原模板的 DOM 验收或新的源码提交身份；
未截图；本次单窗口 fixture 未验证第二次 DOM 关闭批准、队列饱和/异常注入、
其他架构及完整系统矩阵。

### Windows 关闭回归与独立复审

最小可复现入口已纳入 [Windows DOM 关闭回归](../../tests/desktop/windows-close/README.md)，
作为安装后独立消费者，默认不注册 GUI 测试。生产补丁独立审查未发现阻断问题；
Controller.Close 按官方契约停止事件并释放 handler，未另增 token 管理。

2026-10-26 07:15:02–07:15:07 UTC，该回归首轮在实际 WebView2 中自然退出 0，
`vetoLive=true/staleCountsStable=true/errors=0/timeout=false`。root 的三次连续
DOM 调用产生一次否决，随后同文档 Bridge/窗口状态可读；两个依次创建的 child
各收到一次 DOM 批准、一次 closed，各有两次重入请求拒绝 `close_busy`。
关闭后两次请求拒绝 `closed`、程序 close 幂等；销毁 handle 后公共回调捕获对象
释放，后建实例不改变旧计数。它验证正常监听注册/关闭生命周期的可观察行为，
不证明 COM remove 调用次数或每次连续 DOM 调用均触发通知。

该轮 fixture main.cpp SHA-256 为
`53bb9d8a8fd5e7d8508a827445ceef0b798b2c38f1fd6235d3b714a3106ed1b9`，
exe 为 `3cbc834724ca2196318dd2abe51bc252f6b043eb833a3127b89bfad9c130be2e`。
生产候选 windows_webview2.cpp SHA-256 仍为
`104fe6ecb9875490a2ef9a3c1ddbbfd425b426a533116a4028fcf9a01f62ae48`。
通过首轮与最终 fixture 构建链接同一已安装 SDK，期间未重编 SDK；生产候选字节未变，
soui_desktop.lib SHA-256 为
`5a16780d906556cbee4c23bac4000f413794027ffe037ceb5fe4d4b79277c875`。

复审另修正 fixture 的空 handle 失败路径并保留页面错误原因；最终 main.cpp 为
`c9ef110709311efa8790d785bfccb6cb4983217af33ff8bfaa8d74d97bcafe34`。
该版本配置/编译成功、CTest 注册查询为一项，但正常 Start-Process 返回启动拒绝，
现有记录解释为 Windows 应用程序控制拦截。本地化错误原文已乱码，未捕获可靠的
原生数值错误码，不能据此确认具体策略产品。PowerShell 退出 1，未取得应用 PID/
退出码；这与此前 Computer Use 应用审批超时不同，没有改策略、换路线或继续重试。
因此首轮通过只绑定其原始 fixture 身份，不冒充最终 fixture 已运行。
两轮均无视觉验收；注册失败、队列饱和/异常、失效 sender、导航竞态及完整平台矩阵
仍未执行，不改变通用 manifest 的 nativeRuntimeVerified 或发布资格。

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
