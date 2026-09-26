# SDK API

## C++ 接入

```cpp
#include <soui/json_bridge.h>
#include <soui/host/soui_miniblink_host.h>

soui::sdk::json_bridge bridge("app://my-product");
bridge.register_method("app.ping", "app.ping",
  [](std::string_view) { return "{\"message\":\"pong\"}"; });
```

| 接口 | 职责 |
|---|---|
| `soui::sdk::bridge_adapter` | 应用业务适配边界，接收 JSON 和会话关闭通知 |
| `soui::sdk::json_bridge` | 小型应用的标准协议、权限与注册实现 |
| `soui::sdk::host_config` | 内核路径、本地资源根、窗口句柄和适配器 |
| `soui::sdk::soui_miniblink_host` | 启动、消息泵、缩放、显示、重载、销毁 |
| `soui::sdk::shutdown_miniblink_runtime` | 所有 Host 销毁后关闭进程级内核及所有者线程；此后本进程不能重新初始化 |
| `soui::runtime::permission_store` | 与 session/origin 绑定的能力授权及撤销 |
| `soui::runtime::task_store` | 任务状态与取消等底层能力 |
| `soui::runtime::task_service` | 公共长任务调度、会话归属、事件及取消 |

完整窗口、计时器和关闭顺序参考模板的 `app/main.cpp`。C++ 参数和返回类型以 `sdk/include/soui` 的头文件为准。

从 preview.3 起，多个 Host 共用进程级 Miniblink 所有者线程。`destroy()` 只销毁本窗口；即使没有窗口，内核仍保留以支持同进程重建。应用最终退出时，先销毁所有 Host，再调用 `shutdown_miniblink_runtime()`，最后释放应用的 COM 等进程资源。仍有 Host 时返回 `E_RUNTIME_IN_USE`；最终关闭后新 Host 启动返回 `E_RUNTIME_SHUTDOWN`。重复关闭 Runtime 可以成功返回。不同窗口必须使用同一内核路径，否则返回 `E_RUNTIME_BINARY`。这不是独立进程或崩溃隔离承诺。

原生 `__souiNativePostMessage` 的 bootstrap 返回 JSON 字符串，普通请求返回解析为 JSON 字符串的 Promise；连接器必须等待实际结果。仅凭 TypeScript 返回类型声明不能判断原生调用是否同步。

## Web 接入

```ts
import {connectSouiNative, type SouiCoreMethodMap} from '@soui-sdk';
type ProductMethods = SouiCoreMethodMap & {
  'app.ping': { params: {}; result: { message: string } };
};
const client = await connectSouiNative<ProductMethods>();
const result = await client.call('app.ping', {});
```

`connectSouiNative` 负责原生发现、bootstrap、真实事件接线与页面退出清理。同一文档重复调用复用同一个 Promise；选项冲突返回 `E_CONNECTION_OPTIONS`，已有其他分发器时返回 `E_CONNECTION_IN_USE`。失败或 dispose 后需重载文档，不能重新 bootstrap。同一连接属于文档，React 组件卸载只移除本组件订阅；不要在 StrictMode effect 清理时关闭其他组件仍使用的连接。普通浏览器返回 `E_NATIVE_UNAVAILABLE`，模板显示从桌面程序启动的提示。

`SouiNativePort` 仍支持自定义传输：`new SouiNativeClientImpl<ProductMethods>(port, {sessionId, origin, token})`。它的可选 `dispose()` 只释放端口自身监听。调用 `client.dispose()` 幂等地停止业务请求和事件回调，拒绝本地普通等待；这不等于取消原生任务。`disposed`、`pendingRequestCount`、`subscriptionCount` 可以观察本地状态。

`sdk/web/dist` 是模块形式的 SDK 产物；面向 Miniblink49 时仍需经过应用的兼容构建。模板已配置经典脚本和所需转换，不应把 ES module 直接作为 Chrome 49 页面入口。

新连接器默认的 `SouiCoreMethodMap` 声明 `json_bridge` 自带的 probe 与 `task.*` 方法。兼容构造器默认的 `SouiMethodMap` 仍对应工作台服务；业务工程可以传入自己的方法表。最小模板演示公共任务模块，不开放扫描或媒体权限。

## 可选目录选择器

在首次 `bridge.bootstrap` 前调用 `bridge.enable_directory_service()`；重复启用或 bootstrap 后启用返回 `false`。该模块只注册 `desktop.dialog.pickDirectory`，页面在点击回调中调用 `client.call('desktop.dialog.pickDirectory', {})`。返回 `{cancelled:false,path}` 表示用户选定的 UTF-8 目录，`{cancelled:true,path:""}` 表示取消。`SouiDirectoryMethodMap` 可与 Core 等方法表组合。页面不能传入窗口句柄、默认路径、session、token 或 `userGesture` 字段；原生 Host 提供同进程有效窗口，并验证绑定捕获的真实手势。

无手势返回 `E_GESTURE_REQUIRED`，失效窗口返回 `E_INVALID_ARGUMENT`，非 STA 线程返回 `E_THREAD_APARTMENT`。目录对话框是模态调用，会占用当前 Bridge 工作线程直到选择或取消；页面导航、关窗、并发调用及超时尚未完成真实验收。所返回路径不是任意目录的扫描授权，业务扫描根令牌必须由可信原生侧管理。双架构编译、无窗口契约及[Win7 SP1 x86 QEMU 公共取消短测](../verification/production-validation/win7-sp1-x86-qemu-directory-v2-2026-09-25/README.md)已通过；模板真实点击、正向中文路径及其他 V03 门槛仍未通过。设计见 [ADR-0011](adr/0011-public-directory-dialog.md)。

## 可选剪贴板模块

在首次 `bridge.bootstrap` 前调用 `bridge.enable_clipboard_service()`；重复启用或 bootstrap 后启用返回 `false`。此操作只为当前 `json_bridge` 注册 `desktop.clipboard.writeText` 并给该文档会话授权。Host 挂载时传入 `host_config.owner_window`，卸载时撤销窗口引用。应用必须保持窗口有效，不能用空句柄代替。

Web 方法类型为 `SouiClipboardMethodMap`，与 `SouiCoreMethodMap` 组合使用。页面在实际点击回调中直接调用 `client.call('desktop.clipboard.writeText', {text})`，成功返回 `{applied:true}`。原生 Host 在接收调用时记录引擎报告的用户手势；JSON 中的 `userGesture` 参数不会授予权限，并会被拒绝为 `E_INVALID_PARAMS`。缺少真实手势返回 `E_GESTURE_REQUIRED`，窗口失效或文本编码无效返回 `E_INVALID_ARGUMENT`，剪贴板占用或提交失败返回 `E_NATIVE_FAILURE`。文本为 UTF-8，受 Bridge 消息和参数大小限制。模板提供可点击的中文复制入口。

该模块已有双架构编译、契约与参数拒绝测试；[Win7 SP1 x86 QEMU 公共服务短测](../verification/production-validation/win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)也完成中文写入读回并退出 0。模板真实按钮、剪贴板占用竞态、长文本及受限账户仍待独立验收。其他 V03 桌面能力仍未纳入此 opt-in 模块。

## 可选窗口模块

在首次 bootstrap 前调用 `bridge.enable_window_service()`，并在窗口所有者线程收到 `soui::runtime::window_command_message` 时将 `WPARAM` 命令和 `LPARAM` 数字请求 ID 一并交给 `execute_window_command(hwnd, command, lParam)`；模板的 `starter_window` 提供了接线示例。该调用一次性注册 `desktop.window.setTitle`、`beginDrag`、`minimize`、`toggleMaximize`、`close`；重复启用或 bootstrap 后启用返回 `false`。页面以 `SouiWindowMethodMap` 组合方法类型。所有方法只使用 Host 配置的同进程有效窗口，页面不能传入 HWND、session 或手势标志。`setTitle` 接受唯一的 UTF-8 `title` 字段（最多 512 字节），其余方法只接受空对象。拖动、最小化、最大化和关闭要求原生绑定捕获的用户手势；`userGesture` JSON 字段会被拒绝。

跨线程 `setTitle` 通过 SDK 的数字请求 ID 投递到窗口线程，最多等待 250 ms 的实际执行确认；超过时限返回 `E_WINDOW_TIMEOUT`，待处理表最多 64 项。若窗口线程已开始处理，超时后的最终结果可能未知，调用方不能据此断言标题未变化。窗口所有者线程直接调用仍按 Win32 同步执行。`beginDrag` 和 `close` 是排队操作，返回的是接受请求，不保证之后的交互或页面 Promise 一定能在窗口销毁前完成。`minimize` 和 `toggleMaximize` 在当前窗口线程确认后返回。真实点击、旧版 Windows、多窗口及关闭中的响应仍须验收；不要把无窗口契约测试视为这些结果。

## 公共长任务

应用在首次 bootstrap 前调用 `bridge.register_task_handler(kind, handler)`，在处理器中通过 `task_control::report_progress(0..100, message)` 报告进度、定期检查 `cancellation_requested()` 并返回 `task_outcome`。处理器运行在 SDK 的后台工作线程，不能直接修改窗口控件。模板的 `app.count` 展示完整调用。页面以 `client.call('task.start', {kind, operationId, input})` 启动，随后以 `task.query` 读取状态、`client.subscribe(taskId, 'task.progress', listener)` 订阅进度，必要时调用 `task.cancel`，最后等待 `unsubscribe()` 确认。`task.start` 要求三个字段且 `input` 为对象；`kind` 和 `operationId` 只能包含 ASCII 字母、数字、连字符、下划线、点和冒号，长度为 1–128。

同一会话的同一 `operationId` 与相同 `kind`/`input` 重试会返回原 `taskId` 和 `duplicate: true`；不同内容返回 `E_OPERATION_CONFLICT`。任务查询、取消和订阅同时核对 session、origin 和 token；页面导航撤销旧会话并请求取消未完成任务。取消是协作式的：`task.cancel` 先返回 `cancel-pending`，只有处理器退出后才报告 `cancelled`。页面请求超时也不自动取消任务。

默认上限为 2 个原生工作线程、32 个未完成任务、128 个订阅和 1,024 个保留的任务结果。超限返回 `E_BUSY`，旧终态记录按最早序号淘汰；任务结果只保留在进程内，没有持久化保证。返回结果须为不超过 32 KiB 的 JSON 对象，进度与终态消息最多 4 KiB；违反返回约束会形成失败终态。应用的处理器应按自己的数据来源继续验证 `input`，及时响应取消，并自行约束阻塞外部调用的最长时间。当前公共任务模块仍待独立真实页面、Demo、长稳和目标机整组验收。

`subscribe(taskId, eventName, listener)` 保留可调用的移除句柄，同时提供 `ready`、`closed` 和 `unsubscribe(): Promise<void>`。建立失败会拒绝 `ready`；注销立即停止本地回调，等待 `unsubscribe()` 才能判断是否收到原生成功确认。重复移除共享同一次结果；建立期间移除或本地超时后收到迟到成功，会尽力补做一次注销。若传输已丢失确认或文档销毁，原生服务仍须按会话回收自己的订阅，不能依赖页面补偿保证释放。可选 `onError` 观察异步错误及被隔离的业务回调异常。

## 原生应用级托盘（preview.9）

`<soui/runtime/services/desktop/tray_service.h>` 提供可选 `soui::runtime::tray_service`，只链接 `Soui::Sdk` 即可使用，无需 WebView。应用创建隐藏顶层 HWND 和消息循环，在该窗口线程构造、操作及销毁服务；`tray_options` 指定借用的 `owner_window`、待复制的 `icon`、1–65535 的 `icon_id`、应用独占的 `callback_message`（`WM_APP` 范围）以及 UTF-8 `tooltip`。

- `install(options)` 检查窗口归属、复制图标并安装 v4 Shell 图标；重复安装需先 `remove()`。`update_tooltip(text)` 只更新已安装图标，拒绝无效 UTF-8、嵌入 NUL 和超过 127 个 UTF-16 单元的提示，不静默截断。
- WndProc 将消息交给 `dispatch_message(message, wparam, lparam)`；它返回 `handled`、`response` 和 `tray_event::activated/context_menu/none`。只有 `position_valid` 时可用 `x/y`；上下文菜单事件须由应用选择菜单位置。事件仅表示收到匹配通知，不能作为 Web 授权或可信手势凭据。重建消息也通过该入口处理并报告恢复结果。
- `installed()` 表示最近增加图标和 v4 设置均成功，不能证明图标对用户可见；`desired()` 表示 Shell 重建后仍应尝试恢复；`cleanup_pending()` 表示清理尚未确认。安装失败仍须检查返回值和状态，不能仅凭期望状态继续交互。
- 菜单等托盘 UI 操作完成或取消后，调用并检查 `return_focus()`，请求 Shell 将焦点交回通知区。它要求已安装状态，不改变图标生命周期；真实键盘操作仍需桌面验证。
- `remove()` 立即取消恢复意图，再尝试删除；返回失败时可在原线程重试。应用在 HWND 存活时明确移除，随后销毁服务，最后销毁 HWND。析构只做尽力清理。所有操作和状态查询须遵守线程及无重入契约。

服务不创建原生菜单、不注册快捷键、不修改模板最后窗口关闭策略。完整驻留由应用控制器负责。独立接入和待执行的真实 Shell 验收见 [tray-consumer](../tests/tray-consumer/README.md)；设计及失败语义见 [ADR-0012](adr/0012-public-tray-service.md)。

## 错误与超时

`SouiBridgeError` 包含 `code` 和 `requestId`。非法响应、相关 ID 不匹配、权限拒绝、会话失效和页面等待超时都有独立错误。页面超时不代表原生任务已经取消；需要调用服务自己的取消接口。

请求默认最多 64 KiB，参数最多 32 KiB，深度和字段规则由原生 Bridge 限制。应用方法应继续校验自己的参数。不得向不受信任的远程页面开放原生接口。

客户端默认最多 128 个普通等待与 128 个订阅，各可配置至 1,024；`requestTimeoutMs` 默认 10,000、范围 1–60,000 ms，与原生协议一致。`maxMessageBytes` 只能下调 64 KiB 上限。注销另有同订阅上限的控制等待容量，dispose 后可能继续等待确认或超时。

Host 独立限制 256 个普通请求和 128 个取消/释放请求，涵盖排队、执行与回送；客户端较高配置不能提升 Host 上限。过载返回 `E_BUSY`，未完成 ID 重用返回 `E_DUPLICATE_REQUEST`。重载不清空仍占用的原生槽位；排队超时的业务不启动，已经开始的方法仍按业务自身取消协议处理。原生等待记录最多 384 个，响应、超时和页面退出均清理；响应也不得超过 64 KiB。控制请求仍按 FIFO 执行，不能抢占正在运行的方法，长任务应快速返回句柄并在服务自己的有界调度器运行。

## 引用已编译 SDK

安装后在外部工程使用：

```cmake
find_package(SouiSdk 0.3.0 EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE Soui::Sdk)
```

配置时把 `CMAKE_PREFIX_PATH` 指向 `out/install/x64`。头文件和静态库可独立搬移；原生运行依赖仍应按当前架构部署。`tests/package-consumer` 是可以单独构建的最小示例。
