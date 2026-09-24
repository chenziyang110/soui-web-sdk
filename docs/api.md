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

## 公共长任务

应用在首次 bootstrap 前调用 `bridge.register_task_handler(kind, handler)`，在处理器中通过 `task_control::report_progress(0..100, message)` 报告进度、定期检查 `cancellation_requested()` 并返回 `task_outcome`。处理器运行在 SDK 的后台工作线程，不能直接修改窗口控件。模板的 `app.count` 展示完整调用。页面以 `client.call('task.start', {kind, operationId, input})` 启动，随后以 `task.query` 读取状态、`client.subscribe(taskId, 'task.progress', listener)` 订阅进度，必要时调用 `task.cancel`，最后等待 `unsubscribe()` 确认。`task.start` 要求三个字段且 `input` 为对象；`kind` 和 `operationId` 只能包含 ASCII 字母、数字、连字符、下划线、点和冒号，长度为 1–128。

同一会话的同一 `operationId` 与相同 `kind`/`input` 重试会返回原 `taskId` 和 `duplicate: true`；不同内容返回 `E_OPERATION_CONFLICT`。任务查询、取消和订阅同时核对 session、origin 和 token；页面导航撤销旧会话并请求取消未完成任务。取消是协作式的：`task.cancel` 先返回 `cancel-pending`，只有处理器退出后才报告 `cancelled`。页面请求超时也不自动取消任务。

默认上限为 2 个原生工作线程、32 个未完成任务、128 个订阅和 1,024 个保留的任务结果。超限返回 `E_BUSY`，旧终态记录按最早序号淘汰；任务结果只保留在进程内，没有持久化保证。返回结果须为不超过 32 KiB 的 JSON 对象，进度与终态消息最多 4 KiB；违反返回约束会形成失败终态。应用的处理器应按自己的数据来源继续验证 `input`，及时响应取消，并自行约束阻塞外部调用的最长时间。当前公共任务模块仍待独立真实页面、Demo、长稳和目标机整组验收。

`subscribe(taskId, eventName, listener)` 保留可调用的移除句柄，同时提供 `ready`、`closed` 和 `unsubscribe(): Promise<void>`。建立失败会拒绝 `ready`；注销立即停止本地回调，等待 `unsubscribe()` 才能判断是否收到原生成功确认。重复移除共享同一次结果；建立期间移除或本地超时后收到迟到成功，会尽力补做一次注销。若传输已丢失确认或文档销毁，原生服务仍须按会话回收自己的订阅，不能依赖页面补偿保证释放。可选 `onError` 观察异步错误及被隔离的业务回调异常。

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
