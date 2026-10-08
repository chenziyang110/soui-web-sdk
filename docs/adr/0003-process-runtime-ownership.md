# ADR-0003：进程级内核与窗口级 WebView 的所有权

- 状态：proposed，须随公开生命周期变更提交维护者评审
- 日期：2026-09-22
- 关联：[Issue #23](https://github.com/chenziyang110/soui-web-sdk/issues/23)、[ADR-0002](0002-public-sdk-acceptance.md)

## 背景与复现

独立公共 SDK 消费者在现有 `0.3.0-preview.2` 上完成两轮重载，但同进程第二次创建在 `mount` 内以 Windows 访问冲突退出；第二个并存窗口的 `mount` 未返回。原始记录已归档，见[生命周期报告](../acceptance/lifecycle.md)及[证据索引](../../verification/production-validation/index.json)。

当前每个 adapter 创建引擎线程并调用全局 wke 初始化/关闭。上游源码的 `wkeInitialize` 只设置一次 `wkeThreadId`，`wkeFinalize` 关闭 Blink 平台但不恢复初始状态；不能逐窗口重新初始化。`wkeJsBindFunction` 同名注册会覆盖全局函数参数，直接绑定 adapter 指针也不能支持多 WebView。

## 决定

1. 每个进程只创建一个 Miniblink 引擎所有者线程和一个已校验的内核实例。不同窗口共享这个线程，保留各自 WebView、交互端点、Bridge worker、session 和资源根。
2. 关闭窗口只停止该窗口请求、撤销会话、销毁端点/WebView及原生子窗口。最后一个窗口关闭后，应用仍可在同进程重新创建窗口；内核保持已初始化，进程级缓存和线程单独计入资源基线。
3. 增加公开 `shutdown_miniblink_runtime()` 作为应用最终退出入口。存在已挂载 Host 时返回明确错误；零 Host 时在同一个所有者线程关闭内核并 join，之后本进程不再允许启动新 Host。全局析构只提供未显式关闭时的后备清理。模板、Demo 与测试应用须接入最终退出入口。
4. 内核路径由第一次成功使用固定；活跃进程内不能混用另一个内核文件。初始化或 WebView 创建失败要回收窗口级资源，不能误关其他窗口的内核。
5. JavaScript 回调从 `jsGetWebView` 在引擎线程拥有的注册表查找 adapter，再执行原有可信上下文检查。销毁 WebView 前从表移除；不把最后注册的窗口当作全局接收者。
6. 关闭和挂载跨线程操作等待其真实完成，禁止让捕获对象的回调在宿主析构后继续运行。此处不能安全抢占底层内核；测试监督程序负责将挂起记为失败并保存诊断，不把超时终止当作正常关闭。

## 备选方案

- 每窗口独立引擎线程：与已固定的全局内核线程契约冲突，不采用。
- 每窗口独立进程：改变 IPC、部署、隔离及体积模型，留待独立内核路线，不在本次暗中引入。
- 最后一个窗口销毁时 finalize，随后再 initialize：已由独立消费者复现访问冲突，不采用。
- 不调用 finalize，依靠强制结束进程：无法证明正常退出，拒绝作为通过路径。

## 兼容与迁移

保持现有 Host 接入方式，并增加应用最终退出 API；窗口 `destroy()` 不再等同于关闭整个内核。新 SDK 使用未占用的 preview 版本，不能覆盖 preview.2 已产生的包。业务适配器仍须比其 Host 存活更久；最终退出应先销毁所有 Host，再关闭进程级 Runtime。单一窗口之外的 API、模态、托盘等仍需后续独立实现。

## 验证与风险

先重跑相同独立消费者的 reload/recreate/multiwindow，验证跨窗口消息身份、关一窗后其他窗仍可重连及最终 Runtime 关闭。再运行模板和 Demo 默认/React、两架构回归、100 次重载和各模式 1,000 次；长稳及资源趋势单独判断。共享线程不提供进程崩溃隔离，不能通过保留进程级资源掩盖窗口泄漏；报告需区分 WebView 存活、全部窗口关闭但 Runtime 存活、Runtime 已关闭三种采样点。
