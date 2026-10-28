# ADR 0141：窗口所属的原生全局快捷键

## 背景与决定

统一桌面需要在应用不处于前台时接收显式注册的业务快捷键。此契约与应用内 accelerator 分开，不修改旧 Windows Host 服务，也不增加网页 Bridge 权限。

新增 `global_shortcut`（有限 `shortcut_key` 与 modifier 位）、窗口的 register/unregister 方法、独立带窗口 ID 的业务回调及能力标志。首批键为 F1..F11 与明确的方向/导航键，不接受字符、数字、媒体键或原生 keycode；不声称字符布局、物理位置和系统功能键设置完全等价。modifier 为 control/alt/shift/primary，primary 在 Mac 为 Command，其他平台为 Control；至少有 control/alt/primary 一项，不允许裸键或仅 Shift。

每窗最多 32 个注册（包含尚未清理的项）。相同逻辑 ID 或规范化组合不隐式替换。内部 token 与原生 ID 不复用，回调排队有界并验证窗口及当前注册状态；菜单修改不影响快捷键身份。注销先禁回调，失败保留资源记录供重试，关闭对全部注册做清理，最终销毁为 best-effort 而非虚假确认。注册失败不得激活；若回滚失败且仍保留资源，用 `shortcut_cleanup_pending` 明确保留逻辑 ID/配额供注销重试。

原生注册可跨导航保持，但不会自动投递到新网页，事件不证明真实用户手势或系统授权。注册仅由可信 C++ 宿主主动执行；示例不得在启动时自动抢占全局组合。

## 平台与依赖

Windows 使用 RegisterHotKey，不使用键盘 hook；Mac 使用 Carbon EventHotKey，不使用全局键盘监听或请求 Accessibility。Linux 的 X11 实现是条件依赖，缺失时仍能构建并明确 not_supported；Wayland 会话（包括其中的 XWayland）不冒充桌面全局支持。Portal 异步会话/用户重绑定是后续独立契约，不静默降级。

原生组合占用、系统保留键和会话拒绝按实际错误返回，不把所有失败叫作冲突。能力标志仅表示实现，非成功注册承诺。各系统对长按重复存在差异，不保证每次物理按键恰好一次；队列限制仍有效。

## 替代方案、兼容与验证

不暴露 HWND/Carbon ID/X keycode；不通过网页模拟按键，不新增通用“系统权限”开关。原有 API 保持，公开结构追加字段要求重新构建，不承诺二进制 ABI。

聚焦 fake 契约覆盖注册失败、重复、注销失败静默/重试、旧 token、跨窗及关闭重入；条件编译分别检查。Mac 本轮仅编译/fake，不在用户机器实际注册全局快捷键。原生交互未执行不得标为通过。

## 官方依据

- [RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)：组合可能已占用，F12 保留；MOD_NOREPEAT 是 Windows 选项，不当作三端统一物理输入保证。
- [Carbon 64-bit HIToolbox 指引](https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/Carbon64BitGuide/HIToolboxChanges/HIToolboxChanges.html)：不能假设所有 Carbon API 均可用。本轮另只读核对 Xcode SDK 26.5 的 CarbonEvents.h，注册/注销及 pressed/released API 在 arm64/x86_64 语法检查通过；没有实际注册。
- [Xlib 键抓取](https://www.x.org/releases/X11R7.5/doc/libX11/libX11.html) 与 [GDK X11 display error trap](https://docs.gtk.org/gdk3-x11/method.X11Display.error_trap_pop.html)：处理原生冲突及显示连接范围错误，不安装持续进程级错误处理器。
- [XDG GlobalShortcuts Portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html)：需要独立 session/bind 生命周期；本轮不把未实现 Portal 标为 Wayland 支持。
