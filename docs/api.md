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
| `soui::runtime::permission_store` | 与 session/origin 绑定的能力授权及撤销 |
| `soui::runtime::task_store` | 任务状态与取消等底层能力 |

完整窗口、计时器和关闭顺序参考模板的 `app/main.cpp`。C++ 参数和返回类型以 `sdk/include/soui` 的头文件为准。

## Web 接入

```ts
type ProductMethods = {
  'app.ping': { params: {}; result: { message: string } };
};
const client = new SouiNativeClientImpl<ProductMethods>(port, {
  sessionId, origin, token,
});
const result = await client.call('app.ping', {});
```

`SouiNativePort` 定义请求传输和事件订阅。模板提供本地宿主连接示例；在普通浏览器中会显示缺少原生宿主，避免伪造 C++ 执行结果。

`sdk/web/dist` 是模块形式的 SDK 产物；面向 Miniblink49 时仍需经过应用的兼容构建。模板已配置经典脚本和所需转换，不应把 ES module 直接作为 Chrome 49 页面入口。

默认类型表用于工作台内置服务；业务工程可以传入自己的方法表。`query/cancel/subscribe` 是任务协议的便捷方法，只有原生侧注册相应服务后才能使用。最小模板没有默认开放扫描、媒体或任务权限。

## 错误与超时

`SouiBridgeError` 包含 `code` 和 `requestId`。非法响应、相关 ID 不匹配、权限拒绝、会话失效和页面等待超时都有独立错误。页面超时不代表原生任务已经取消；需要调用服务自己的取消接口。

请求默认最多 64 KiB，参数最多 32 KiB，深度和字段规则由原生 Bridge 限制。应用方法应继续校验自己的参数。不得向不受信任的远程页面开放原生接口。

## 引用已编译 SDK

安装后在外部工程使用：

```cmake
find_package(SouiSdk 0.2.0 EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE Soui::Sdk)
```

配置时把 `CMAKE_PREFIX_PATH` 指向 `out/install/x64`。头文件和静态库可独立搬移；原生运行依赖仍应按当前架构部署。`tests/package-consumer` 是可以单独构建的最小示例。
