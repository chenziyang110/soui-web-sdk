# SDK API

## 未发布统一桌面 API

Linux x86_64 已完成限定真实窗口、Bridge 和同源模块资源链路验证，使用带记录
补丁的 WebKitGTK 2.52.6 自构建运行时；详见
[2026-10-22 原生验证](linux-native-validation-20261022.md)。不代表其他架构、
系统发行 WebKit 包、macOS/Windows 或生产验收通过。

Linux 文档失败后的新脚本提交返回 E_LOAD；等待原生 reload 或旧求值通道已取消
时返回 E_NAVIGATION。正常文档启动不要求额外 ready 门槛，窗口级操作仍保留。
在途脚本取消不保证其副作用未发生。
macOS 明确失败或等待重载的文档返回 E_NAVIGATION，进程终止仍优先返回
E_WEB_PROCESS；正常 precommit 求值不受影响。

Linux 构建可显式提供 SOUI_DESKTOP_WEBKIT_HELPER_DIR；已知部署缺失 helper 时
initialize 返回 E_WEBKIT_RUNTIME。默认未知不预判，详见[统一桌面入口](unified-desktop.md#linux-显式-helper-部署预检)。

前端 ESM 薄适配、TypeScript 声明与资产解码见 [前端接入](desktop-frontend-adapter.md)。

`application_resource_directory()` 按实际可执行文件位置定位默认前端目录，
返回 `path_result {status, value}`，不依赖工作目录。应用目录和 CMake staging
入口见 [应用交付目录](desktop-application-staging.md)。

显式启用 `options.enable_resource_server` 可把应用选定 HTML 和根内资产通过
每窗口独立的 loopback HTTP 源加载；默认关闭，标准模块/fetch 的真实三引擎
验收尚待完成。边界见 [本地 HTTP 资源源](unified-desktop.md#显式本地-http-资源源)。

显式启用 `options.enable_asset_read` 可使用 `window.souiDesktop.readAsset(path)`
读取根内小型资产；默认关闭，保留内置方法名 `soui.asset.read`，返回 MIME、
字节数和 base64。边界与迁移见 [资产读取契约](unified-desktop.md#显式本地资产读取)。

独立的 `Soui::Desktop` 使用 `<soui/desktop/app.h>`，提供 `application` 窗口组、
`create_window`、每窗口 Bridge、原生层级菜单、托盘及 command/lifecycle 回调。
其线程、生命周期、限制和打包方式见 [统一桌面入口](unified-desktop.md)。
这不是以下 Windows `Soui::Sdk` / TypeScript API 的逐项跨平台替代；既有行为
和测试结论不会自动转移到新后端。

## 0.4 现代后端与环境探测

可选 `Soui::WebView2` 导出 `<soui/host/webview2_host.h>`；UI STA 异步准备后使用相同 `webview_host`。`show()` 恢复隐藏视图并保留会话。公共 `<soui/host/runtime_profile.h>` 提供实际系统/PE/架构探测和候选档位选择；Web SDK 导出 `inspectSouiWebFeatures`、`requireSouiWebFeatures`。这些探测不授予原生权限，不代表目标环境认证。完整初始化、通知、关闭和部署契约见 [现代后端](modern-backend.md)。

## 动作契约与受控界面（preview.10）

`json_bridge::register_action(name, capability, contract_json, contextual_handler)` 注册同步原生动作。契约包含 `protocol: "soui.action/1"`、标题、说明、`inputSchema` 与 `outputSchema`；通过原有会话授权后先验证输入，再执行处理器并验证成功结果。业务错误仍走原有Bridge错误路径。契约不会自动把动作注册为task kind，也不能绕过公共任务处理器自己的校验。

schema只支持明确的object/properties/required/additionalProperties:false、array/items/minItems/maxItems、string/minLength/maxLength/enum、integer/number/minimum/maximum、boolean、null。输入/输出根必须是object；对象必须声明properties且禁止额外字段，数组必须声明items与maxItems，字符串必须声明maxLength。未知关键字、无界或过深schema在注册时拒绝。字符串长度按Unicode标量，数字须可在Web安全表达；完整规范和限制由公共 `action_contract.h` 与Web类型约束。

`set_session_capabilities(...)` 由C++应用在首个bootstrap前设置已注册能力白名单，同时限制方法发现和直接执行；默认保持旧行为。它属于文档会话授权，不能据此宣称拥有独立Agent主体认证。网页自报actor或手势不改变权限。

Web `discover()` 保留已校验的可选 `method.action`。`souiToolDefinitions(catalog)` 只为这些方法生成通用工具定义；`validateSouiActionInput/Output` 提供相同子集的客户端检查，原生检查仍是最终边界。工具定义本身不执行动作或授予权限。

`createSouiGui()` 为应用明确注册的元素提供 `register`、`snapshot`、`perform`、`waitFor` 和 `dispose`。节点有稳定ID、role/name、enabled/busy、显式应用state和命名动作。调用只进入注册处理器，不执行任意选择器或脚本；禁用、断开、忙碌、移除与销毁均有错误。等待有上限，调用超时不代表原生任务已取消，实际处理器结束前仍保留busy。示例见模板。0.5 的原生 UIA 与本地 Agent 分别显式启用，边界如下；页面 GUI 注册本身不会自动启用它们。

## 可选原生 UIA 与本地 Agent（0.5）

`Soui::Uia` 导出 `<soui/runtime/accessibility/windows_uia_provider.h>`。使用 `Build.ps1 -WithUia` 或 `Soui.ps1 build --with-uia` 构建；CMake 消费者显式请求 `Uia` 组件并链接 `Soui::Uia`。应用在实际 HWND 所有者线程初始化 COM、创建 provider、发布审核后的公开投影，并转发 `WM_GETOBJECT` 与选定的唤醒消息。普通 owner 消息泵继续调用 `pump()`，导航或退出时先撤销并等待 `close()` 返回 `S_OK`，再释放控制器与 HWND。模块不扫描 DOM，也不自动接入 Host 业务。

首批控件为按钮、非敏感单行输入、单选列表与只读任务状态。`publish()` 接纳状态后，由有界消息泵发送属性、结构及焦点通知；成功 Invoke 发送 Invoked 通知。Invoke 的返回表示排队受理，业务结果须另行观察。策略默认拒绝；应用将动作绑定到既有 typed native 业务入口，UIA 不提供 Agent 身份或原生用户手势。敏感输入不提供 Value pattern。构建、线程及关闭细节见 [UIA 设计](adr/0047-finite-windows-uia-provider.md)、[状态通知](adr/0109-uia-state-notifications.md)和[构建与交付](adr/0110-uia-build-delivery.md)。

同步 SetValue、Select、SetFocus 成功表示业务和公开投影已提交。Windows 在窗口线程调用时，provider 通过已配置的唤醒消息同步处理一批有界命令；应用必须正确转发消息。回调重入拒绝，未完成的排队请求取消并超时；其他线程仍最多等待 250 毫秒。详见[同步 owner 派发](adr/0116-uia-owner-command-dispatch.md)。

`json_bridge::create_local_endpoint()` 在业务注册完成、首个页面 bootstrap 之前显式创建停止状态的 Windows 本地端点。应用配置允许的调用主体与能力后才启动；`allow_local_action()` 只公开已经注册的 typed action，长任务通过 `register_local_task_family()` 复用同一 Bridge 的任务服务。页面授权、UIA 桌面访问和本地 Agent 授权分别处理。端点由 Bridge 持有，应用必须在销毁 Bridge 前完成真实停止与 owner 收尾；详见[端点契约](adr/0046-windows-local-agent-endpoint.md)和[本地任务 family](local-task-families.md)。

`enable_gui_commands()` 为明确注册的 GUI 操作接入任务协议；`bind_gui_document()` 由原生 owner 绑定实际 Host 文档身份和有效性检查。关闭时先 `close_gui_commands()`，由 owner 持续 `poll_gui_commands()` 至 closed，再销毁 Host。它不会把任意页面 handler 转成可远程调用的方法；原生效果与页面确认各有独立校验，见 [GUI 授权边界](adr/0054-gui-command-authority.md)。这些接口可用不代表读屏器、所有参考应用或目标系统已通过验收。

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
| `soui::sdk::wait_for_miniblink_runtime_shutdown` | 应用最终退出时，在总期限内泵送窗口消息并等待上述异步关闭；错误或超时保持失败 |
| `soui::runtime::permission_store` | 与 session/origin 绑定的能力授权及撤销 |
| `soui::runtime::task_store` | 任务状态与取消等底层能力 |
| `soui::runtime::task_service` | 公共长任务调度、会话归属、事件及取消 |

完整窗口、计时器和关闭顺序参考模板的 `app/main.cpp`。C++ 参数和返回类型以 `sdk/include/soui` 的头文件为准。兼容内核必须同时提供 `wkePrepareWorkerShutdown` 与 `wkeFinalizePrepared`；缺少任一接口时在初始化前返回 `E_DEPENDENCY_API`，不能混用旧 DLL。

从 preview.3 起，多个 Host 共用进程级 Miniblink 所有者线程。`destroy()` 只销毁本窗口；即使没有窗口，内核仍保留以支持同进程重建。应用最终退出时，先销毁所有 Host，再完成 Runtime 关闭，最后释放应用的 COM 等进程资源。非阻塞 `shutdown_miniblink_runtime()` 返回 `E_RUNTIME_SHUTDOWN_PENDING` 时仍须在原关闭期限内继续调用，不能把首次提交当成成功。最小应用可在 UI 线程调用 `wait_for_miniblink_runtime_shutdown(std::chrono::seconds{20})`；它只重试 pending，按单调总预算处理有限批次消息，保留已读取的 `WM_QUIT`，负预算返回 `E_RUNTIME_SHUTDOWN_ARGUMENT`，零预算只尝试一次，超时返回 `E_RUNTIME_SHUTDOWN_TIMEOUT`。不能从引擎所有者或仍持有 Host 的回调中等待。真实错误及超时不证明内核已释放。仍有 Host 时返回 `E_RUNTIME_IN_USE`；最终关闭后新 Host 启动返回 `E_RUNTIME_SHUTDOWN`。重复关闭 Runtime 可以成功返回。不同窗口必须使用同一内核路径，否则返回 `E_RUNTIME_BINARY`。这不是独立进程或崩溃隔离承诺。

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

## 公共能力发现（preview.9）

`json_bridge` 自带 `bridge.describe`。该方法通过普通 Bridge 请求校验 session、origin 和 token，仅列出已注册且该会话有权访问的方法。它不会调用业务方法、打开对话框或新增授权。

```ts
const client = await connectSouiNative();
const catalog = await client.discover({pageSize: 32, maxMethods: 1024});
const hasClipboard = catalog.methods.some(item => item.name === 'desktop.clipboard.writeText');
// hasClipboard 表示已注册且当前获授权；写入仍须真实手势、有效窗口等前置条件。
```

`client.describe({cursor: 0, limit: 32})` 获取一页。返回 `{protocol: 'soui.capabilities/1', sdkVersion, methods: [{name, capability}], nextCursor}`，终页 `nextCursor` 为 null。只接受可选 cursor/limit；cursor 为非负安全整数，limit 为 1–64，默认32。按方法名稳定排序，游标属于 bootstrap 后冻结的注册表；不要把游标或发现结果跨文档会话复用。

`discover()` 自动分页，默认最多保留1,024项，最大可配置4,096项。超过上限返回 `E_LIMIT_CAPABILITIES`；格式错误、不前进游标、跨页重复或版本变化返回 `E_INVALID_RESPONSE`；原生拒绝、旧宿主不支持、超时和 dispose 仍显式失败，不返回伪造或部分清单。与普通 call 一样，发现请求受并发/大小/超时约束。低层 `client.call('bridge.describe', ...)` 只有通用 envelope 校验，优先使用上述高层入口进行发现结果校验。

该清单不是参数 schema、能力永久承诺或 Agent 操作授权。自定义方法仍应验证业务参数；页面与 Agent 均不能通过 JSON 伪造 native user gesture。注册方法名限 ASCII 字母、数字、点、连字符、下划线、冒号，1–128字节；capability 为1–512 UTF-8字节且可编码为 JSON。注册失败返回 false。

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

## 同文档会话续期（preview.11）

原生应用在 bootstrap 前调用 `json_bridge.enable_session_renewal()`，使新授权具有 `bridge.session.renew` 能力。Starter 与 Workbench 已显式启用；自定义适配器通过 `bridge_transport.enable_session_renewal()` 注册通用处理器，并在原生固定能力集中授予该项。bootstrap 的可选 `lease` 来自 `permission_store.lease`，包含 `generation`、`expiresInMs` 和 `renewAfterMs`；未启用的应用仍使用 30 分钟有限授权。

标准连接器自动将 lease 交给原客户端并在到期前五分钟续期。`client.renewSession()` 返回本次租约；早于本地续期时间时只返回当前值，在途调用共享同一结果。`client.sessionLease` 返回剩余期限副本。自定义 port 可传 `lease` 和非负 `leaseElapsedMs`，后者扣除 bootstrap 往返消耗的时间。SDK 保守计算本地期限，不能据此延长原生期限。普通业务发送保持同步，不等待续期、不自动重放；任务和订阅对象继续使用原实例。

原生续期仅接受 `{operationId}`，只能由未到期的当前令牌在剩余期限不超过五分钟时创建新 30 分钟令牌。旧令牌最多重叠三十秒且不超过其原到期时间；期间可继续原权限内的普通业务，只能以同一 operationId 重取已经完成的续期结果。会话、来源与能力集合不变，generation 递增，重试不增加有效期。错误包括 `E_RENEW_TOO_EARLY`、`E_UNAUTHORIZED`、`E_SESSION_EXPIRED`、`E_ENTROPY_UNAVAILABLE`；不得把过期后重新 bootstrap 当作原会话恢复。

公共 `permission_store.resolve(session, origin, token, capability)` 返回本次校验及稳定的 `authorization_scope`。transport 始终重建原生 `bridge_request.authorization`，JSON 不得提供它；直接调用 `task_service` 的原生代码必须提供正确的 scope。资源比较 scope 身份加 session/origin；同一 session 重新 issue 的独立 grant 不能访问旧资源。关闭顺序必须先 `permission_store.revoke_session` 再服务 `shutdown_session`，新资源在提交锁内复验 scope。已接纳任务协作式结束；到期后不新投递其订阅事件，关窗负责最终资源清理。

原生 `scope->scope_id()` 是续租不变的 `soui-scope-<十进制序号>` 关联 ID，可预测且不是凭据。仅保证同一 SDK 模块装载实例内不复用，不承诺跨进程、重启或独立静态 SDK 副本唯一性；uint64 耗尽时 issue 抛出 overflow_error，不回绕。任务所有权仍比较实际对象，不能凭字符串查找或授予授权。`permission_store.revoke_scope(scope)` 只撤销本 store 持有的对象及所有 bearer 别名，空、其他 store 或已移除对象返回 false。先撤销，再调用 `task_service.shutdown_scope(scope)` 清理该对象的订阅并请求协作取消；活跃或空 scope 返回 false，true 不表示工作线程已结束，重复清理安全。一个范围的关闭不影响同文档其他 grant。排队工作在业务回调执行提交前复验授权；到期或撤销不回滚已经提交的业务。事件出口仍是原页面全局 sink，本 API 未提供外部 Agent 的按 scope 定向事件。

一次 Web 续期最多三次尝试，共享同一个 operationId 和最多 25 秒的总预算；dispose 终止计时器，迟到回应不能复活客户端。Host 和实际注入 resolver 均保留单独的一项续期容量，但不能抢占正在执行的业务方法。休眠、长模态或阻塞超过原生期限仍会失败，需应用明确处理重新加载。行为边界见 [ADR-0014](adr/0014-same-document-session-renewal.md)，真实八小时与 Win7 验收另行留证。

## 公共长任务

应用在首次 bootstrap 前调用 `bridge.register_task_handler(kind, handler)`，在处理器中通过 `task_control::report_progress(0..100, message)` 报告进度、定期检查 `cancellation_requested()` 并返回 `task_outcome`。处理器运行在 SDK 的后台工作线程，不能直接修改窗口控件。模板的 `app.count` 展示完整调用。页面以 `client.call('task.start', {kind, operationId, input})` 启动，随后以 `task.query` 读取状态、`client.subscribe(taskId, 'task.progress', listener)` 订阅进度，必要时调用 `task.cancel`，最后等待 `unsubscribe()` 确认。`task.start` 要求三个字段且 `input` 为对象；`kind` 和 `operationId` 只能包含 ASCII 字母、数字、连字符、下划线、点和冒号，长度为 1–128。

同一原生授权 scope 中 `operationId` 与相同 `kind`/`input` 重试会返回原 `taskId` 和 `duplicate: true`；不同内容返回 `E_OPERATION_CONFLICT`。同一文档的不同 grant 或不同 permission_store 的相同 operationId 独立。任务查询、取消和订阅核对本次 token 的授权结果，并按 session、origin 和实际授权对象确认资源归属；轮换不重启任务、不重置 sequence。页面导航撤销旧会话并请求取消未完成任务。取消是协作式的：`task.cancel` 先返回 `cancel-pending`；只有业务回调尚未执行便取消，或处理器明确返回 `task_cancelled` / `user_cancelled`，才报告 `cancelled`。已执行处理器返回的 success、partial、failure 仍形成对应真实终态；收到取消请求后仅退出并返回 success 不会自动变成 cancelled。页面请求超时也不自动取消任务。

默认上限为 2 个原生工作线程、32 个未完成任务、128 个订阅和 1,024 个保留的任务结果。超限返回 `E_BUSY`，旧终态记录按最早序号淘汰；任务结果只保留在进程内，没有持久化保证。返回结果须为不超过 32 KiB 的 JSON 对象，进度与终态消息最多 4 KiB；违反返回约束会形成失败终态。应用的处理器应按自己的数据来源继续验证 `input`，及时响应取消，并自行约束阻塞外部调用的最长时间。当前公共任务模块仍待独立真实页面、Demo、长稳和目标机整组验收。

`task_store`、`task_service` 和 `json_bridge` 不传 policy 时，每个 Store 默认采用
`default_task_store_request_quota`（128 MiB，即 134,217,728 字节）。额度覆盖该
Store 的私有分配请求及事务期间仍存活的缓冲、失败终态预留，不是进程总内存
或浏览器缓存限制。诊断关闭不会关闭额度。原生显式 policy 构造仍接受
`std::nullopt`（未配置）、零（拒绝正分配）或自己的字节额度；页面不能调整它。
零额度也可能在构造时因容器初始化的正分配而抛出配额异常；此时对象未构造，
不能读取它的快照。构造成功时零额度仍拒绝后续正分配。
Bridge 准入超过额度返回 `E_TASK_STORE_QUOTA`，直接 Core 调用抛出
`task_store_request_quota_exceeded`。拒绝保留任务身份和原状态，后续合法任务
仍可执行；需要超过公开 Bridge 边界的大型 Core 数据时，请显式选择原生 policy。
详见 [默认额度与迁移](adr/0101-default-task-store-request-quota.md)。

`task.query` 接受 `{taskId}` 或 `{operationId}`，对象恰好有一个字段；两者不能同时提供，也不能附加其他字段。标识均为 1–128 字节，仅允许 ASCII 字母、数字和 `-_.:`。`client.query(taskId)` 保留，新增 `client.queryOperation(operationId)`；直接 `call('task.query', params)` 使用同一参数校验。原生 operation 查询只在本次实际活跃授权 scope 中定位，并再次核对实际对象、session/origin 和保留记录；同一 scope 续租不改变归属，不同 grant/store 或重连的新 scope 不能读取旧任务。查询不启动、重放或取消任务，不修改快照或保留顺序。结果字段与 taskId 查询相同。未知或被淘汰的记录返回 `E_TASK_NOT_FOUND`，失效或缺能力的授权返回 `E_UNAUTHORIZED`；not-found 不证明从未执行，不授权自动重试。

已授权 catalog 中无 action 的 `task.query` 可获得工具描述。受限 schema 只表达两个可选的有界字符串和禁止额外属性，**不能单独表达互斥或 ASCII 字符集合**；描述明确要求 exactly one，TS 运行时及原生最终校验负责完整约束。schema 通过或工具出现均不是执行授权。参见 [operation 查询迁移](migration/task-operation-query.md) 与 [ADR-0032](adr/0032-read-only-task-operation-query.md)。

`subscribe(taskId, eventName, listener)` 保留可调用的移除句柄，同时提供 `ready`、`closed` 和 `unsubscribe(): Promise<void>`。建立失败会拒绝 `ready`；注销立即停止本地回调，等待 `unsubscribe()` 才能判断是否收到原生成功确认。重复移除共享同一次结果；建立期间移除或本地超时后收到迟到成功，会尽力补做一次注销。若传输已丢失确认或文档销毁，原生服务仍须按会话回收自己的订阅，不能依赖页面补偿保证释放。可选 `onError` 观察异步错误及被隔离的业务回调异常。

## 原生应用级托盘（preview.9）

`<soui/runtime/services/desktop/tray_service.h>` 提供可选 `soui::runtime::tray_service`，只链接 `Soui::Sdk` 即可使用，无需 WebView。应用创建隐藏顶层 HWND 和消息循环，在该窗口线程构造、操作及销毁服务；`tray_options` 指定借用的 `owner_window`、待复制的 `icon`、1–65535 的 `icon_id`、应用独占的 `callback_message`（`WM_APP` 范围）以及 UTF-8 `tooltip`。

- `install(options)` 检查窗口归属、复制图标并安装 v4 Shell 图标；重复安装需先 `remove()`。`update_tooltip(text)` 只更新已安装图标，拒绝无效 UTF-8、嵌入 NUL 和超过 127 个 UTF-16 单元的提示，不静默截断。
- WndProc 将消息交给 `dispatch_message(message, wparam, lparam)`；它返回 `handled`、`response` 和 `tray_event::activated/context_menu/none`。只有 `position_valid` 时可用 `x/y`；上下文菜单事件须由应用选择菜单位置。事件仅表示收到匹配通知，不能作为 Web 授权或可信手势凭据。重建消息也通过该入口处理并报告恢复结果。
- `installed()` 表示最近增加图标和 v4 设置均成功，不能证明图标对用户可见；`desired()` 表示 Shell 重建后仍应尝试恢复；`cleanup_pending()` 表示清理尚未确认。安装失败仍须检查返回值和状态，不能仅凭期望状态继续交互。
- 菜单等托盘 UI 操作完成或取消后，调用并检查 `return_focus()`，请求 Shell 将焦点交回通知区。它要求已安装状态，不改变图标生命周期；真实键盘操作仍需桌面验证。
- `remove()` 立即取消恢复意图，再尝试删除；返回失败时可在原线程重试。应用在 HWND 存活时明确移除，随后销毁服务，最后销毁 HWND。析构只做尽力清理。所有操作和状态查询须遵守线程及无重入契约。

托盘服务只管理图标；菜单与快捷键由下面的独立服务负责。完整驻留由应用控制器负责。独立接入见 [tray-consumer](../tests/tray-consumer/README.md)；托盘语义见 [ADR-0012](adr/0012-public-tray-service.md)。

## 原生菜单与全局快捷键（preview.10）

`<soui/runtime/services/desktop/menu_service.h>` 的 `menu_service::show(owner, items, x, y)` 接收 `menu_item` 值树并同步返回 `menu_result`。命令、分隔线、子菜单分别使用 `menu_item_kind`；命令 ID 为全树唯一的 1–65535，子菜单/分隔线 ID 为 0。支持 UTF-8 标签、禁用、勾选；最多 256 项、8 层，每标签 1,024 UTF-16 单元、全树标签 64 KiB UTF-16 字节。分隔线不得附带文本、子项、勾选或禁用状态。owner 属于当前进程和构造线程；若为 child，服务激活其顶层父窗口。

检查 `response.ok()` 后才可消费 `has_selection` / `command_id`；返回 ID 还经过启用祖先检查。无选择不等于已确认取消或菜单曾可见，`native_error` 保留原生诊断。菜单运行时会分派应用消息，期间不能销毁 owner 或服务。只有同服务的 `cancel()` 可在跟踪期间重入；其他同线程菜单弹出返回 `E_BUSY`。失败清理留在 `cleanup_pending()` 中，先检查 `cleanup()`，再销毁服务。托盘入口完成菜单后另行调用 `tray_service::return_focus()`。

`<soui/runtime/services/desktop/global_shortcut.h>` 的 `global_shortcut` 拥有一项注册。`register_shortcut({owner, id, modifiers, virtual_key, no_repeat})` 的 ID 为 0–0xBFFF，修饰键为 `shortcut_modifiers` 中 ALT/CONTROL/SHIFT/WIN 的组合，键码 1–254 且不得为 F12。默认禁止按住重复触发。检查响应的 `ok()`、`code` 和 `native_error`；系统保留或占用的组合不保证注册成功，也不能把所有错误都称为冲突。

WndProc 将 WM_HOTKEY 的完整参数交给 `dispatch_message()`，仅在 `activated` 为真时执行应用动作；匹配消息不是可信手势。`unregister_shortcut()` 立即停止事件消费；失败保留 `cleanup_pending()` 供重试。成功注销后 `retired()` 为真，同一对象不再注册；新对象使用新 ID，不得在同一 HWND 生命周期内复用注销过的 ID，以免接收排队旧消息。先确认注销，再析构服务及销毁 owner。设计及限制见 [ADR-0013](adr/0013-public-menu-shortcut-and-residency.md)。

## 本地连接授权（0.5.0 preview）

`permission_store.create_local_binding(session, origin)` 创建本 store 的不可复制原生连接对象。它不认证 Windows 客户端，也不启用 Agent 端点；可信应用须先完成身份验证、原生授权和窗口/文档生命周期检查。页面 JSON 不能提供此对象。

`issue_local(binding, capabilities, methods)` 接受明确的两份非空白名单，每份最多 64 项，返回 `{token, scope}`。普通页面 `resolve/check/lease/renew` 拒绝本地 bearer；`resolve_local(binding, token, method, capability)` 也拒绝页面 bearer、其他 store 或其他实际连接对象。相同 session/origin 字符串不能重建连接归属。方法白名单独立于 capability，执行边界必须重新调用 `resolve_local`；`scope->allows` 不能代替该检查。

`lease_local` 和 `renew_local` 同时要求 `bridge.session.renew` 的 method 与 capability，并保留原有有限租约、旧 bearer 重叠和幂等规则。`binding->close()` 不可逆地停止授权，重连须新建对象。关闭与已开始发行/续租可能竞争；返回 token 不保证可执行，但发行始终交付精确 scope，供应用调用 `revoke_scope` 和 `shutdown_scope`。随后仍须完成任务、回调和 I/O 清理；close 或 revoke 不是完成 barrier。详见 [ADR-0036](adr/0036-local-connection-bound-grants.md)。

`capability_registry::allow_local_action(method)` 和 `json_bridge::allow_local_action(method)` 仅在注册阶段显式开放已有 typed action；默认关闭，plain 方法和保留控制方法不能开放，facade 在 bootstrap 后冻结注册。它不添加页面 capability，也不审核业务数据是否敏感。可信端点持有实际 binding 和私有 bearer，调用 `bridge_transport::handle_local(request, binding, bearer)`；请求提供的 session/origin/token/authorization 被原生绑定覆盖，`native_user_gesture` 固定为 false。执行前核对 method 与 capability，返回前核对同一实际 scope；未知、隐藏、过期或未授权的本地方法返回统一 `E_UNAUTHORIZED`。

`describe_local` 使用同一连接授权过滤发现列表，再分页；canonical `bridge.describe` 不执行应用业务 callback。本地业务错误和异常返回固定错误，非法输出不回送原文。参数与页面 envelope 在解析阶段检查深度，不可表示的数值返回固定 schema 错误。输出 schema 约束结构，应用仍须审核字段和业务授权。当前内建 `task.start/query/cancel` 为 plain 方法，不能通过此入口开放；安全任务包装层、真实传输、OS 主体验证和发送边界尚待实现。关闭后的检查可丢弃已完成结果，不能回滚已执行的业务或代替 I/O 清理。详见 [ADR-0037](adr/0037-shared-local-action-dispatch.md)。

## 可选 Miniblink 命名缓存观测

包含 `soui/host/soui_miniblink_host.h`，在宿主启用 scalar 诊断时调用 `request_miniblink_cache_sample()` 显式提交一个普通 Engine 任务。返回 `pending` 表示任务在途；排队满、关闭开始或接口缺失分别保留 busy/unavailable 状态。`read_miniblink_cache_metrics(out)` 仅尝试读取已完成副本，不提交任务或等待。读取可能仍是旧样本；使用 `has_completed_sample`、`inflight`、`runtime_retired` 和原 `sampleQpc` 判断，不能把当前读取时间当作新采样时间。

新可选 ABI 为版本 1 / 216 字节，与原 renderer 168/264 字节接口分开。加载内核没有新接口时返回 unavailable；不据路径相等授予二进制身份。组件数值只在对应 native status 为 READY 时有效，其他状态不能解释成零。采样不创建缓存、触发回收或修改限额。

覆盖范围是现有 Blink MemoryCache 的 live/dead 与软 prune 配置、Skia 已附着全局 glyph strikes、全局保留 resource records 及实际配置。它不包含 detached/TLS 缓存、其他实例、被外部持有的已淘汰对象、全部字体/图像 heap 或 V8 compile cache。策略阈值不是统一 heap 硬上限；进程缓存可在最后宿主关闭后保留。详见 [ADR-0070](adr/0070-finite-cache-observation.md)。

## 错误与超时

`SouiBridgeError` 包含 `code` 和 `requestId`。非法响应、相关 ID 不匹配、权限拒绝、会话失效和页面等待超时都有独立错误。页面超时不代表原生任务已经取消；需要调用服务自己的取消接口。

请求默认最多 64 KiB，参数最多 32 KiB，深度和字段规则由原生 Bridge 限制。preview.9 的标准 JSON 响应也限制为64 KiB；业务返回或可选元数据使完整响应超限时返回 `E_LIMIT_MESSAGE_SIZE`。发现页按完整响应大小自动缩页。应用方法应继续校验自己的参数。不得向不受信任的远程页面开放原生接口。

客户端默认最多 128 个普通等待与 128 个订阅，各可配置至 1,024；`requestTimeoutMs` 默认 10,000、范围 1–60,000 ms，与原生协议一致。`maxMessageBytes` 只能下调 64 KiB 上限。注销另有同订阅上限的控制等待容量，dispose 后可能继续等待确认或超时。

Host 独立限制 256 个普通请求和 128 个取消/释放请求，涵盖排队、执行与回送；客户端较高配置不能提升 Host 上限。过载返回 `E_BUSY`，未完成 ID 重用返回 `E_DUPLICATE_REQUEST`。重载不清空仍占用的原生槽位；排队超时的业务不启动，已经开始的方法仍按业务自身取消协议处理。原生等待记录最多 384 个，响应、超时和页面退出均清理；响应也不得超过 64 KiB。控制请求仍按 FIFO 执行，不能抢占正在运行的方法，长任务应快速返回句柄并在服务自己的有界调度器运行。

## 引用已编译 SDK

安装后在外部工程使用：

```cmake
find_package(SouiSdk 0.5.0 EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE Soui::Sdk)
```

配置时把 `CMAKE_PREFIX_PATH` 指向 `out/install/x64`。头文件和静态库可独立搬移；原生运行依赖仍应按当前架构部署。`tests/package-consumer` 是可以单独构建的最小示例。

## 本地 typed 任务 family

`task_service::register_local_family` 和 `json_bridge::register_local_task_family` 原子注册具名 start/query/cancel 三动作，绑定已有 handler kind，仅供明确授予 method AND capability 的实际本地 scope。实际 scope/family/kind 隔离在核心锁内复验；页面、其他 family 与 plain 任务互拒，终态仅返回一次缓存的安全投影。既有 worker、队列和有界保留共用，not-found 不授权自动重试。详见 [接入与限制](local-task-families.md) 和 [ADR 0039](adr/0039-local-typed-task-families.md)；本接口不代表本地端点或 OS 主体认证完成。

## 整数帧节拍

头文件 soui/host/frame_rate_policy.h 提供 is_supported_frame_rate(unsigned) 与
clamp_frame_rate_to_display(requested, display)。合法目标为 30–1000 Hz 整数；无效请求返回 0，
未知显示配置以 60 Hz 为上限，合法配置直接限制目标而不向 60/120/144 档位取整。
frame_pacer::start 使用相同范围。工作台的 desktop.rendering.setTargetRate 校验整数参数，
保留请求值与实际调度值的区分。移动窗口或更改显示设置后会重新查询配置。
调度目标、系统配置和实际呈现是不同观测；诊断覆盖与计时器回调不证明物理刷新率。

资源缓存观测允许 discardable 模式报告正的 resourceByteLimit；它是驻留记录淘汰阈值。零表示未配置，resourceEffectiveSingleLimit 仍保持独立。8 MiB 策略不代表全部图像堆或进程内存上限，详见 ADR-0102。

# 原生后端失败与关闭边界（实验性统一桌面入口）

Linux 当前文档加载失败或 WebProcess 终止后，旧文档排队的 loaded/message/reload
回调与未完成求值被撤销；窗口级 dispatch/close 仍可用，显式原生 load/reload
可开启新代际。macOS 区分系统关闭通知与主动关闭，清理幂等且不递归 close。
这些内部修复不新增权限或公共 API；具体决策见 ADR-0134，原生运行验收单独记录。

