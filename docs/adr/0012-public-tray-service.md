# ADR-0012：应用级公共托盘服务

- 状态：proposed；合并公共 API 前需维护者评审
- 日期：2026-09-26
- 开发版本：`0.3.0-preview.9`
- 关联：[生产验证方案](../roadmap/0.3-sdk-validation.md)、[能力矩阵](../acceptance/capabilities.md) V04-02

## 背景

SDK 已有共享进程 Runtime 和多个独立 Host，但没有托盘、原生菜单、全局快捷键和无窗口驻留的公共契约。将托盘放在页面 Bridge 中会让它随文档撤销，与应用驻留寿命冲突。本切片补齐原生托盘服务和安装后独立消费者；菜单、快捷键、完整 Workbench 驻留和真实平台验收仍属 V04 必需工作。

## 决定

1. 增加可选 C++ `tray_service`。应用创建并拥有隐藏顶层 HWND 和其消息循环，寿命长于业务窗口；在同一创建线程调用安装、提示更新、消息分派、移除和析构。SDK 不另起线程，不创建 HWND，不修改应用的最后窗口关闭策略。必须在 HWND 销毁前显式移除并销毁服务。
2. 安装只接受当前进程、当前线程的有效顶层 HWND；拒绝子窗口和 `HWND_MESSAGE`。后者不能接收广播，无法承担 `TaskbarCreated` 恢复。应用独占所选 HWND、1–65535 的图标 ID 与 `WM_APP` 范围内的回调消息；这组身份不得与其他服务重用。服务借用 HWND，复制传入 HICON 并只释放自己的副本。
3. 每次 `NIM_ADD` 成功后都检查 `NIM_SETVERSION(NOTIFYICON_VERSION_4)`。版本设置失败时回滚删除；回滚也失败须保留待清理状态并报告错误，不能标为可交互。安装失败不自动无限重试；应用决定是否在收到重建通知后重试。
4. `remove()` 首先取消期望驻留，删除失败仍返回错误并保留重试清理能力。移除后收到 Explorer 重建通知不得复活图标。析构尽力清理不能代替显式移除的已确认结果。失去 Shell 图标后仍保留应用期望状态，重建通知触发重新安装并重新设置版本；失败保留可重试状态。
5. SDK 不向 WndProc 注册 `this`，不保存用户回调；应用把消息传入 `dispatch_message()`，收到值类型事件后自行执行动作。这避免在 Shell 通知期间调用应用回调产生重入销毁。未安装、错误回调消息或不匹配 ID 均不产生激活事件。`TaskbarCreated` 是提示，不是安全凭据；此接口不授予 Web 权限或原生用户手势。
6. v4 通知从 `lParam` 解码事件和 16 位图标 ID。激活支持 `NIN_SELECT` / `NIN_KEYSELECT`，上下文菜单支持 `WM_CONTEXTMENU`。只在 Win32 明确定义 `wParam` 为坐标的通知上返回有符号坐标；上下文菜单事件不承诺 `wParam` 坐标，应用须另取指针位置或按键盘策略定位。
7. UTF-8 tooltip 严格校验编码、嵌入 NUL 及 UTF-16 长度，最多 127 个 UTF-16 单元，不静默截断。v4 使用 `NIF_SHOWTIP` 显示普通提示。更新失败保留先前已确认的提示和期望状态。
8. 应用结束托盘触发的菜单/UI 操作后，调用 `return_focus()` 请求 `NIM_SETFOCUS`，并检查结果；仅在图标已确认安装且所有者仍有效时允许。消费者在菜单选择或 Esc 取消后均调用，防止键盘焦点停留在隐藏窗口。它不修改安装、期望或清理状态，Shell 拒绝必须可观察。

## 备选方案

- 页面或 Bridge 持有托盘：不能覆盖所有业务窗口关闭后的驻留。
- SDK 私建消息线程：扩大线程同步和析构契约，当前应用已有原生消息循环，无需增加线程。
- 使用消息窗口：收不到 Shell 的顶层广播。
- 自动改变模板 `OnClose`：会破坏现有最小应用行为；后续以应用明确选择的驻留控制器接入。

## 兼容与迁移

新增接口不改变已有窗口、Bridge、任务或模板关闭行为。SDK 内容变化使用 preview.9；preview.8 及更早冻结载荷的验收仅属于原身份。托盘服务只面向可信原生应用，暂不注册 Web 方法。消费者使用安装后的公开头文件和 `Soui::Sdk`，无需内核 DLL 或 WebView。

## 验证

私有、未安装的平台调用边界用于无桌面单元测试：失败安装、版本设置与回滚、恢复失败及重试、移除后广播、错误线程/窗口/ID、UTF-8/长度边界、事件解码、复制图标释放一次。测试不得把模拟广播当作真实 Explorer 恢复。

独立消费者提供明确选择的短测与交互模式，记录原始退出码和有界关闭步骤。两架构先独立安装、仓库外构建；真实图标可见、鼠标/键盘通知、Explorer 重启、失败清理及 Win7 x86 运行须在桌面空闲时串行执行。整组 V04 还要求原生菜单、快捷键、三窗口业务隔离、无窗口驻留和退出资源回收，不能由此单模块短测替代。

## 原生契约依据

- [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)：每次增加图标后设置并检查版本。
- [NOTIFYICONDATAW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw)：v4 通知 ID/事件布局、坐标适用范围与提示大小。
- [Taskbar](https://learn.microsoft.com/en-us/windows/win32/shell/taskbar)、[Window features](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)：重建广播与消息窗口限制。
