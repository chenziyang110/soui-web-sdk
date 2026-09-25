# ADR-0009：公共剪贴板写入的窗口所有权

- 状态：proposed；双架构原生编译与拒绝路径通过，真实写入及维护者评审待完成
- 日期：2026-09-25
- 关联：[0.3 验证方案](../roadmap/0.3-sdk-validation.md)、[公共能力矩阵](../acceptance/capabilities.md)

## 背景

`clipboard_service::write_text` 原先调用 `OpenClipboard(nullptr)`，随后清空并提交 `CF_UNICODETEXT`。Win32 在这种调用方式下把剪贴板所有者设为空，`SetClipboardData` 将失败；因此已有方法不能作为 V03 的可用公共能力。转换文本时还必须区分空文本与无效 UTF-8，避免把编码错误变成清空剪贴板的操作。

## 决定

1. `write_text` 显式接收应用窗口句柄，在取得权限和真实手势后验证它仍存在且属于当前进程。没有有效所有者时，在打开或清空剪贴板前返回 `E_INVALID_ARGUMENT`。调用方必须让该窗口在调用结束前存活。
2. 文本转换和可移动内存分配先于 `OpenClipboard`；无效 UTF-8、嵌入 NUL、分配失败及剪贴板被占用不会清空现有内容。成功调用 `SetClipboardData` 后内存所有权交给系统。Win32 不提供清空后的原子回滚；若最终提交失败，应报告 `E_NATIVE_FAILURE`，不能声称旧剪贴板已保留。
3. Workbench 的现有适配器将宿主窗口传入公共服务。新项目的 C++ 调用从 `write_text(context, text)` 迁移为 `write_text(owner_window, context, text)`；这是 preview 期间的源代码签名变更。默认 Core 不因此自动注册高权限 Web 方法。
4. Web 侧公共剪贴板写入通过 `json_bridge.enable_clipboard_service()` 显式注册。宿主在 JS 绑定处读取引擎手势状态并随原生请求传递；JSON 中的 `userGesture` 字段不能授予手势，且该方法只接受 `text`。对话框仍需 UI 线程调度；剪贴板的公共注册只完成 V03 的一个子能力。

## 验证与限制

x86/x64 Release 已重新编译 SDK、Workbench 和启用模块的模板；纯转换及空窗口/无效窗口/无效文本拒绝测试通过，公共 Bridge 契约还覆盖缺少原生手势、伪造 JSON 手势和缺少所有者。模板 Web 类型检查及构建通过。[Win7 SP1 x86 QEMU 公共服务短测](../../verification/production-validation/win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)已在隔离的客体中读取实际提交的中文文本，退出 0；原始串口、系统和程序身份已校验。模板真实 UI、剪贴板占用竞态、受限账户、断开远程会话和长文本仍待独立验收。
