# ADR-0010：可选公共窗口 Bridge

- 状态：proposed；有界标题调度已完成双架构编译与无窗口原生测试，真实线程验证待执行
- 日期：2026-09-25
- 关联：[公共 SDK API](../api.md)、[能力矩阵](../acceptance/capabilities.md)

## 背景

`window_service` 已在 SDK 中，但通用 Web 方法注册仍由 Workbench Demo 持有。新项目复制 Demo 注册会同时复制其参数解析、手势信任和窗口生命周期假设，不能证明 SDK 的公共窗口能力。

## 决定

1. 应用在首次 bootstrap 前显式调用 `json_bridge::enable_window_service()`；默认 Core 不授予窗口控制。Host 将自己的窗口句柄传入适配器，卸载时清空。公共服务在调用 Win32 前验证该句柄仍是当前进程的有效窗口。
2. 公共方法只接受标题或空参数。Bridge 从已认证的请求填充 session、origin、token；手势只取原生 JS 绑定的状态，不接受 JSON 中自报的 `userGesture`。标题允许无手势，其余四项要求原生手势。
3. 拖动、最小化和最大化经 `window_command_message` 交给 HWND 所属线程；应用必须在该线程处理消息。拖动消息执行时再次核对鼠标左键仍按下。关闭向同一窗口投递 `WM_CLOSE`，返回值只说明投递被接受。
4. 跨线程 `setTitle` 只向窗口投递数字请求 ID，标题由 SDK 内部有界请求表持有，在 HWND 所属线程执行。调用方等待最多 250 ms 的实际执行确认；超时撤销尚未开始的请求并返回 `E_WINDOW_TIMEOUT`，最多允许 64 个待处理标题。若窗口线程已经进入执行，超时后的最终结果仍可能未知，调用方不得把超时当作“标题一定未变化”。窗口所有者线程直接调用时保持 Win32 的直接执行语义。模板和 Demo 必须将消息的 `LPARAM` 一并交给 SDK 命令处理器；标题仍不允许携带指针跨消息队列。

## 验证

x86/x64 SDK 与模板 Release、SDK Web 构建和模板 TypeScript 检查通过；公共原生契约覆盖启用冻结、假手势、缺手势、无效参数和缺少所有者。仓库外安装的两个架构 SDK 均由独立消费者调用新接口并退出 0。当前修订的 SDK、模板和 Demo 已在双架构重新编译，六项无窗口原生测试各自通过。可选的 `soui_desktop_unicode_tests --desktop-roundtrip` 和 `--window-thread-timeout` 已在双架构编译，分别覆盖真实 HWND 操作和窗口线程停滞/恢复；宿主长测期间两项 GUI 场景**尚未执行**。上述已执行测试没有创建真实 WebView，响应顺序、旧版系统与资源释放仍待目标环境执行。
