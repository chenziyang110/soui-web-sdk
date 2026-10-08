# ADR-0011：可选公共目录选择器

- 状态：proposed；公共接口已实现并完成双架构无窗口契约，真实对话框验收尚未完成
- 日期：2026-09-25
- 关联：[0.3 验证方案](../roadmap/0.3-sdk-validation.md)、[公共能力矩阵](../acceptance/capabilities.md)

## 背景

SDK 已有 `directory_picker` 原生类，但 `desktop.dialog.pickDirectory` 的 Web 注册、参数解析和扫描根令牌返回值仍由 Workbench Demo 持有。新项目不能只安装 SDK 就获得该对话框能力。Demo 的扫描令牌是业务授权，不应当把页面提交的任意路径视作用户已选择的目录。

宿主 Bridge 工作线程以 STA 初始化 COM；现有 `directory_picker` 使用 `SHBrowseForFolderW` 的新样式。微软文档要求该调用前初始化 COM，且新样式不能在 MTA 中调用：[SHBrowseForFolderW](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shbrowseforfolderw)。直接调用 C++ 服务的其他宿主也必须满足这一线程条件。

## 决定

1. 公共 `json_bridge` 在首次 bootstrap 前显式启用目录模块；只注册 `desktop.dialog.pickDirectory`，默认 Core 不授予该权限。方法不接受页面指定的 HWND、路径、会话或手势字段；宿主提供同进程有效窗口，手势只取原生绑定记录。
2. 页面成功选择后收到 `{cancelled:false,path}`；用户取消收到 `{cancelled:true,path:""}`。路径表示用户所选位置，不是任意目录的扫描授权；需要扫描根令牌的产品必须在可信原生侧绑定该选择。Workbench 仍可用同一个 SDK `directory_picker` 产生自己的令牌，但其业务注册不能被误称为公共 Web 模块。
3. `directory_picker` 在显示对话框前检查窗口归属，并确保调用线程处于 STA；MTA 冲突和无效窗口返回明确错误。对话框是模态操作，会占用当前 Bridge 工作线程直至选择或取消；并发请求、关窗、导航和超时期间的行为在真实页面验收前不列为生产通过。

## 必需验证

双架构公共契约覆盖启用时机、假手势、缺手势、无效参数、无效窗口及 MTA 拒绝，SDK 和模板均已编译；安装后的 x86/x64 SDK 也分别由仓库外消费者只用公开头文件和库编译并退出 0。[可选取消场景](../../verification/production-validation/win7-sp1-x86-qemu-directory-v2-2026-09-25/README.md)已在实际 Win7 SP1 x86 QEMU 客机执行：独立 STA Bridge 工作线程与另一线程的可见 HWND 打开真实 Shell 对话框，观察到对话框后关闭并核对公共 JSON 取消返回，退出 0。模板仍须从点击打开真实对话框，校验取消与含中文目录的正向路径；还需检查受限账户、网络路径、窗口关闭/导航时的模态行为和 Bridge 队列延迟。该短测不能替代完整 V03 验收。
