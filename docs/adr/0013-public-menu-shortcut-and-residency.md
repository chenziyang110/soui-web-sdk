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

## Workbench 焦点拒绝的关闭政策（2026-11-02）

菜单已成功返回有效命令时，Workbench 原来先调用 `return_focus()`；任何失败都会
立即请求退出，使尚未投递的窗口恢复命令被丢弃。无选择返回后，同一失败也会
立即关闭驻留。这把 Shell 的焦点确认失败与必须停止业务的所有权错误耦合。

本次只调整 Workbench 此调用点的关闭政策：精确的 failure / `E_TRAY_FOCUS`
仍经原 `desktop_result` 记录 `ok=false` 并锁存非零退出码，另记
`tray.focus-warning`，但不因此立即 `request_exit`。只有菜单结果成功、
`has_selection=true` 且未进入退出时，才走原 `queue_action`；无选择时不派发
命令，也不将它认证为真实 Esc。warning 中的 `commandFlowContinues` 表示
继续命令流程的决定，不证明消息已投递、命令已执行或窗口已恢复。

其它焦点校验错误、菜单/清理/Host 错误仍走原失败和关闭路径。已有退出请求
不撤销，已有非零退出码不清除；成员栈、`operation_scope`、原消息投递和
关闭顺序保持。SDK `return_focus()` 的 FALSE / `E_TRAY_FOCUS` 契约、原生
失败测试和严格 `tests/tray-consumer` 全部保留，没有新增公共 API、线程、
重试、焦点效果回退或成功定义。

备选是继续立即关闭、把 API 错误改成 success，或只在日志中忽略该错误。
本次选择保留严格失败码而继续应用流程，既避免丢弃已确认命令，也保留
原始失败。此项是 Workbench 应用政策修复，不解释 NIM_SETFOCUS 根因、
不证明焦点已归还；#27/#29、Win7 与完整驻留/生产资格继续未完成。

适用验证须绑定当前源码、安装 SDK、EXE 与完整载荷，在 x64/x86 Release
分别执行：实际选择已关闭槽位的打开/恢复命令，核对成功菜单选择、原焦点
拒绝及 warning、后续命令路由和真实窗口/Host ready；另做真实菜单 Esc，
确认无命令且继续驻留。最后显式退出，保留原非零结果、自然清理与外部
Job 闭合。`commandFlowContinues`、PostMessage 成功或原生菜单无选择不能
替代实际路由/界面证据。未修改的安装后严格消费者仍须保留焦点失败后
停止动作/非零退出的结果；不得将功能路由通过写成原 API 或 SDK 资格通过。

### 实际验证范围

`0.6.1-preview.1` 本地未发布候选已完成 x64/x86 Release 标准构建，固定
MSVC v143 `14.44.35207` Hostx64 与 Windows SDK `10.0.19041.0`；每架构
三项 fake 平台服务契约和十一项纯驻留关闭策略 CTest 均通过，共 14 项、49 个
契约组。两架构隔离 VM 样本均实际选择关闭槽位的命令 3，随后槽位 3 新代际
创建并 ready；焦点结果仍为 `E_TRAY_FOCUS`，warning 和退出码 1 保留。
随后命令 11 触发原有有序关闭，外部 Owned 均观察 root exit 1、Job 活跃数 0、
无超时且未强清，`passed=false` 不转换为通过。真实菜单 Esc 只在 x64 完成，
x86 未执行；无选择返回本身仍不是 Esc 证明。

三个早期产品 GUI 样本因操作流程未在原 200 秒期限内完成而被 Owned 超时清理，
原件和失败保持；它们没有产品自然关闭证明，也不据此归因为产品菜单或关闭故障。
原生 `NIM_SETFOCUS` FALSE、SDK 严格契约与原托盘消费者均未修改，本次没有执行
新的安装后严格消费者。Win7、完整 #27/#29 与生产资格继续未完成。

源码、controller、EXE、完整运行载荷散列及实际事件时间见[本轮维护记录](../maintenance/tray-focus-20261102.md)。
记录绑定当时已冻结输入；后续文档或重建不覆盖旧 raw，也不把调用方业务流程恢复
写成公共 API 成功或焦点根因已修复。

## preview.3 调用方串行分派决定（实现前记录）

`.2` 已取得十八次公共 focus 成功响应，但 Server Workbench 和 Win10 两个严格
消费者仍实际收到非延后的 callback `E_BUSY`，使最终退出码锁存 1。busy 检查早于
原通知的 ID/kind 解码；旧 raw 的 notification=0 仅表示 SDK event none，不能证明
原通知属于可忽略种类。Win10 x64 另有一次快速更新后快捷键的 E_MENU_FOREGROUND，
该失败单独保留，不推断其已由本决定解决。具体冻结样本见维护记录。

决定在两个原生调用方内串行分派 callback，保持 `.2` SDK 的忙时拒绝与原 NIM BOOL
契约。各应用以独立 RAII 深度仅标记 tray 自身成员调用；在该成员栈内收到 callback
时，保存原 message/wParam/lParam 到 UI 线程拥有的固定容量 FIFO，不调用 SDK，也
不制造成功 response。TaskbarCreated 继续立即分派，保留准备取消和重建提示语义。
队列容量 64；溢出须记录明确错误并使应用失败，不能覆盖旧通知或自动循环重试。

在既有 timer 的安全边界（tray 调用、应用操作、菜单和业务分派均已返回）按 FIFO
重放，每轮至多八项且只处理进入该轮前已有的项目。FIFO 只保证已入队项目之间的
顺序，未入队 callback 继续按 Windows 原分派顺序处理，不承诺全局输入排序。
重放本身须有防重入标志；重放期间在 tray 成员栈内新入队的项目留给后续 timer。
guard 析构只降低深度，不执行 SDK、不重放或投递循环。
真实 SDK response 仍按原严格处理，包括 E_BUSY、owner 错误、重建失败及 NIM FALSE。
退出先撤销队列意图、记录丢弃数，成员返回后再按既有关停契约移除服务与 HWND。

Workbench 与独立消费者各拥有内部辅助代码，SDK 不依赖 Demo 或测试目录；安装后
消费者把其应用辅助头一同复制到仓库外，仍只使用公共安装 SDK。不新增公共 API、
线程、额外消息泵、Shell 权限、输入模拟或超时。新日志明确区分原通知入队、未调用
SDK、重放及实际 response，并保存原参数和 Win32 通知 word；raw 数值不作为可信手势。

备选方案是忙时吞掉 callback E_BUSY、在 SDK busy 前猜测通知不可操作，或延后全部
模态菜单期间的消息。前两者会掩盖真实失败或依赖旧 raw 未记录的信息，后者会让
鼠标通知积压。因此选取只覆盖 tray 自身成员调用的应用队列，不改变 SDK busy 合同。

该后续内容使用独立 `.3` 身份；`.2` 构建、载荷与失败保持冻结，回退须恢复完整 `.2`
应用与 SDK 安装。验证须覆盖原参数顺序/宽度、容量、重入、重放中新消息、退出清空、
实际 SDK 回应和 TaskbarCreated 旁路；再执行双架构安装消费及真实取消/键盘/窗口恢复。
以上为实现前决定；后续实现及限定验证另行记录，不追溯修改 `.2` 的结果。

### preview.3 实际实现与验证

两调用方已按上述边界实现；双架构 Release 标准构建、安装后独立消费及每架构
14 项非 GUI CTest、76 组行为测试通过。新增六组回归保留真实 SDK busy 拒绝、原始
NIM FALSE、Taskbar 取消及 owner 失败，再验证调用方原参数队列和成员返回后重放。
原 35 组 SDK 托盘回归保持，未通过忽略失败取得通过。

Server 两消费者、两 Workbench 及 Win10 两消费者共六个新用例完成真实菜单取消、
原位键盘重开及显式退出；Workbench 另完成第三窗恢复。十八次公共 focus 响应均
成功，六例 native0、Owned completed0/Job0，无超时或强清。真实 callback 全部
direct，queued/replayed 均为零，不能把该 GUI 结果归因为队列重放或声称其真实路径
已覆盖。原 NIM BOOL 没有单独 raw 字段，非零只按冻结严格成功路径推断。

随后在同一 Server 以原冻结程序追加两架构正常 Explorer Restart；各一次 Taskbar
处理成功、图标实际重现，后续菜单/键盘/提示/快捷键可用并自然退出 0。结合 GUI
操作及十份原件验证恢复，不把 sourceVerified=false 的广播单独当作重启来源认证。
两例仍无真实 FIFO 入队或重放。

Windows 前台策略、旧快速快捷键 E_MENU_FOREGROUND、Win7 OS32、其它系统的恢复、
长期驻留和完整 #27/#29 门槛继续保留。源码、BuildID、载荷、原件和结果散列见
[维护记录](../maintenance/tray-focus-20261102.md)；未发布或提升生产资格。
