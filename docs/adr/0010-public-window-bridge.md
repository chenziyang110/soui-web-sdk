# ADR-0010：可选公共窗口 Bridge

- 状态：proposed；双架构编译与无窗口契约通过，真实窗口交互和线程阻塞评估待完成
- 日期：2026-09-25
- 关联：[公共 SDK API](../api.md)、[能力矩阵](../acceptance/capabilities.md)

## 背景

`window_service` 已在 SDK 中，但通用 Web 方法注册仍由 Workbench Demo 持有。新项目复制 Demo 注册会同时复制其参数解析、手势信任和窗口生命周期假设，不能证明 SDK 的公共窗口能力。

## 决定

1. 应用在首次 bootstrap 前显式调用 `json_bridge::enable_window_service()`；默认 Core 不授予窗口控制。Host 将自己的窗口句柄传入适配器，卸载时清空。公共服务在调用 Win32 前验证该句柄仍是当前进程的有效窗口。
2. 公共方法只接受标题或空参数。Bridge 从已认证的请求填充 session、origin、token；手势只取原生 JS 绑定的状态，不接受 JSON 中自报的 `userGesture`。标题允许无手势，其余四项要求原生手势。
3. 拖动、最小化和最大化经 `window_command_message` 交给 HWND 所属线程；应用必须在该线程处理消息。拖动消息执行时再次核对鼠标左键仍按下。关闭向同一窗口投递 `WM_CLOSE`，返回值只说明投递被接受。
4. `setTitle` 暂沿用现有同步 Win32 调用。它可能在窗口线程挂起时阻塞 Bridge 工作线程；该风险与真实线程/关闭回归是 V03 未通过的条件，后续需要有界的 UI 线程调度设计，不能由本次契约通过推断生产可用。

## 验证

x86/x64 SDK 与模板 Release、SDK Web 构建和模板 TypeScript 检查通过；公共原生契约覆盖启用冻结、假手势、缺手势、无效参数和缺少所有者。仓库外安装的两个架构 SDK 均由独立消费者调用新接口并退出 0。这些测试没有创建真实 WebView，窗口操作、响应顺序、旧版系统与资源释放仍待目标环境执行。
