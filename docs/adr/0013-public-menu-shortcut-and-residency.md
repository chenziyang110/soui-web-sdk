# ADR-0013：公共原生菜单、快捷键与显式驻留模式

- 状态：proposed；公共 API / 生命周期修改合并前需维护者评审
- 日期：2026-09-26
- 开发版本：`0.3.0-preview.10`
- 关联：[ADR-0012 托盘](0012-public-tray-service.md)、[生产验证方案](../roadmap/0.3-sdk-validation.md) V04

## 背景与所有权

preview.9 已增加托盘，但独立消费者仍自己操作 HMENU，SDK 没有快捷键模块；Workbench 的窗口关闭及 SOUI 默认顶层窗口销毁都会结束消息循环。只保留托盘图标不能证明三窗口独立工作或无窗口驻留。

新增能力只供可信原生应用显式调用，不注册 Web 权限，也不把 Win32 消息视为可信手势。SDK 不创建后台线程或应用窗口；所有服务及其状态查询归构造线程，所借 HWND 必须属于当前进程/线程。应用在该 HWND 存活时明确清理，再析构服务并销毁窗口。同步原生调用会重入消息循环，不允许在成员调用尚未返回时销毁服务；普通重入操作返回 `E_BUSY`。

## 公共菜单

`menu_service` 接收值类型树（命令、分隔线、子菜单）、同线程 HWND 和屏幕坐标，创建原生菜单并返回所选命令 ID，不保存应用回调或暴露 HMENU。支持 Unicode 文本、禁用与勾选、子菜单。输入先完整验证再创建资源：最多 256 项、8 层，标签最多 1,024 UTF-16 单元且总计最多 64 KiB UTF-16 字节；严格 UTF-8、禁止嵌入 NUL；命令 ID 全树唯一且为 1–65535，分隔线与子菜单不占命令 ID，形状冲突明确失败。

使用 `TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON`，避免一条选择同时经返回值及 WM_COMMAND 执行。返回 ID 还须属于本树可用命令（含祖先启用条件）；无效选择明确失败。返回 0 只表示没有可执行选择，不能单凭它证明用户取消或菜单可见；保留可取得的原生错误诊断。菜单为 Windows 原生模态 UI，期间应用消息继续分派。`cancel()` 仅在本服务正在跟踪菜单时调用 `EndMenu`，允许应用在关闭/超时期间结束本服务的菜单；没有活动菜单时不操作其他人的菜单。

菜单服务在跟踪前把 owner 设为前台（已为前台也接受）；系统拒绝时返回明确错误，跟踪结束后向 owner 投递 WM_NULL 以完成原生菜单关闭流程。托盘键盘焦点仍由独立 `return_focus()` 负责，不由菜单服务猜测调用来源。

服务拥有全部创建的 HMENU；子菜单成功挂接后由父菜单拥有。任何创建/挂接/跟踪/取消/销毁错误均可观察。销毁失败保留待清理根句柄，可通过 `cleanup()` 重试；新弹出操作不能覆盖待清理句柄。析构尽力清理不等于已确认清理。托盘触发的菜单完成后，应用仍须调用 `tray_service::return_focus()`；菜单模块不依赖托盘模块。

## 公共全局快捷键

`global_shortcut` 对象拥有一项成功注册；多个对象可同时持有多个快捷键。应用提供独占 HWND/id，ID 使用应用范围 0–0xBFFF；调用者不得在同一 HWND 生命周期内复用已注销 ID，避免旧队列消息被新注册接收。构造不注册；`register_shortcut()` 失败可以重试，成功注册后必须显式 `unregister_shortcut()`。成功注销后该对象退休，不再复用；需要新的注册时建立新对象并分配新 ID。

修饰键仅接受 ALT/CONTROL/SHIFT/WIN 位，键码为 1–254；保留给调试器的 F12 拒绝。默认使用 Windows 7 已支持的 `MOD_NOREPEAT`，调用者可明确关闭。Windows 保留或其他程序占用的组合以实际注册结果为准，不能承诺可抢占。失败保留原生错误码，不把所有错误都简化为“冲突”。同一对象不能重复注册并让系统累积旧键。

注销请求先停止事件分派；注销失败保留待清理状态并允许重试，不能恢复激活或当作成功释放。`dispatch_message()` 仅在有效注册时匹配 WM_HOTKEY、完整 ID、键码和修饰键并返回值类型激活结果；未注册、注销中、错误消息/ID/组合均不触发动作。它只描述匹配消息，不能验证消息发送方或授权。

## Workbench 显式驻留

新增 `--resident`，保留默认运行方式。驻留控制器创建隐藏顶层 HWND，持有托盘、公共菜单与全局快捷键；主窗加两个子窗各自持有独立 adapter、Host 和会话，共享进程 Runtime。业务窗口使用 SOUI 显式普通窗口类型，关闭某窗只关闭其会话与窗口，不发布线程退出。对象释放延后到该窗口消息处理返回之后；关闭全部业务窗仍保留控制器与图标。

托盘菜单和快捷键恢复现有窗口或重建已关闭的窗口，不依赖 Host 的 `hide()` 作为恢复机制。只有显式退出或不可恢复初始化失败才统一停止输入、结束菜单、注销快捷键、移除托盘、关闭全部业务 Host/adapter，再最终关闭 Runtime，最后销毁控制器 HWND 和退出消息循环。必须检查 SOUI 自身的 OnDestroy / Run 行为，不能绕过其资源清理或用窗口样式伪装压制退出。启动失败及部分窗口创建失败也按同一所有权顺序收回。

驻留控制器在全部槽位释放后显式关闭 Runtime 并记录结果；既有 wWinMain 的最终关闭调用保留为幂等后备，两处均检查错误。非驻留退出仍使用原有后备路径。

## 备选与迁移

不将通用菜单/注册调用继续留在 Demo 或测试应用；也不自动改变所有模板的退出行为。此版增加公共 API，SDK 内容版本升至 preview.10；preview.9 冻结输入与全部历史结果保持原身份。模板最小结构不新增驻留业务。

## 验证与限制

私有未安装平台边界验证菜单参数、资源转移、禁用祖先、取消/重入/清理失败和选择过滤；快捷键验证保留键、冲突及其他原生失败、重复注册、错误线程、消息过滤、注销失败停止事件、退休和资源释放。独立安装消费者必须实际使用公开 API，不调用 SDK 私有实现或复制通用 Win32 实现。

真实菜单选择/取消、快捷键冲突与实际按键、托盘恢复、三窗口互不干扰、任务中关窗、空窗驻留和明确退出需单独串行测试；Win7 SP1 x86 及资源/长期门槛分别保留原始结果。模拟成功、编译或短测不能关闭完整 V04。其他 QEMU 仍运行时不启动桌面负载。

依据：[RegisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)、[UnregisterHotKey](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-unregisterhotkey)、[TrackPopupMenuEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-trackpopupmenuex)、[DestroyMenu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroymenu)。

## 0.5 驻留退出的本地总期限（已采用，真实 GUI 验证待完成）

旧控制器在 Host 返回可重试关闭状态时不断重新设置 50ms timer；消息循环提前
结束后的 GetMessage 也没有总期限。显式退出、smoke 超时或启动异常因此可能
永久保留所有者而不返回。本修复只处理这个 Workbench 私有退出路径。

首次实际退出请求开始固定 10,000ms 本地关闭预算。启动异常、外部 WM_QUIT
后的补充清理以及析构都使用同一首次起点；后续退出请求、timer、消息或
destroy 重试不能更新它。这是采用的应用政策，依据是保留 owner 的
可重试关闭契约及现有有限清理方式；不是 SDK 的响应时间或目标系统保证。
原 ready 45s、resident smoke 180s、外部 Owned 与默认非驻留行为不变。

仅精确 E_BUSY / E_CURSOR_CLOSE_PENDING 允许重试。原非零退出码和原 Host
结果日志保持，未知 Host 错误先记录，再调用既有 cleanup_fail_stop。
到期同样记录实际 elapsed/budget 后 fail-stop；不会设置 shutdown_complete，
不会释放仍借用的 slot、HWND、Host 或 Bridge，也不会强制 Runtime 卸载。

返回前清理泵以最多 50ms 且不超过剩余期限的
MsgWaitForMultipleObjectsEx + PeekMessage(PM_REMOVE) 等待，继续原 SOUI OnMsg。
WM_QUIT 仍只是退出请求；实际 Host / HWND 清理成功才允许释放。每次等待、
分派及 native 调用返回后的检查共用期限。同步 native/消息调用仍可能阻塞；
本地期限是 cooperative 检查，外部 Owned 期限负责该类失败，不作硬抢占声明。
服务 cleanup 原有分类及结果处理保持不变，本切片没有新增服务清理资格。

纯逻辑验证直接编译生产使用的无平台调用 deadline / Host 分类 helper，覆盖
首次 tick=0、重复退出不续期、边界、最后 1ms、unsigned tick 跨回绕、
时钟倒退 fail-closed、精确 pending 与未知/嵌入 NUL 错误等负面情况。
x64 与 Win32 各九个纯 C++ 用例和对应 CTest 已实际通过，并完成独立复核；
两次预期 CLI 拒绝保留原 generic false 和监督器清理事实。
这些测试不能证明真实 resident/Host/菜单或线程已闭合。
实际关闭、smoke timeout 与 fail-stop 原生特例仍由 Root 在 Owned 下独立运行；
更广生命周期、系统、长稳、资源和发布资格保持未关闭。

依据：
[MsgWaitForMultipleObjectsEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex)、
[PeekMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagew)。
