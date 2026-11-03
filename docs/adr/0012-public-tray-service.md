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

## 2026-11-02 候选：return_focus 的有界状态准入

本节状态为 proposed；本地候选实现与验证依现有修复授权推进，公共合并仍需维护者
评审批准。本文记录候选设计，不构成 SDK 实测或生产通过结论。
原生依据封存时，`0.6.1-preview.1` 的 SDK、版本及严格托盘消费者未因实验修改。
后续本地实现使用独立 `0.6.1-preview.2` 身份，不覆盖 `.1` 的冻结内容与资格。
原决策、原 BOOL 失败和 [ADR-0013 的 Workbench 调用方政策](0013-public-menu-shortcut-and-residency.md)
保持各自身份；本节不宣布 #27/#29、Win7 或生产资格通过。

### 背景与有限依据

独立实验中，仅执行 `SetForegroundWindow` + `WM_NULL` barrier 可使原 NIM 返回 TRUE，
但实际 Shift+F10 打开任务栏通用菜单，未路由到图标第二菜单；该简化方案不采纳。
候选增加对目标 Shell 线程状态的有界观察，不把 barrier 返回当作焦点工作已完成。

单个状态准入样本位于 `out/tray-focus-20261102/raw-state-admission`，独立 EXE SHA-256
为 `5c49195e35f661616591d36686277da7a537aba3cda8df699f2ec18f39ebc980`。
首次成功查询的 active 为 Shell、focus 为 NULL，谓词不满足；准备起点后 17 ms 首次
匹配并通过最终校验，一次等待请求 5 ms、实际 15 ms。唯一原 NIM 于 17,558 ms 返回
TRUE；五次后续观察均为 `ToolbarWindow32`，实际 Shift+F10 显示 SECOND MENU 并以
Esc 结束，原生及 Owned 均自然退出 0、Job 活跃数 0、无超时或强清。两份 raw 与两份
模块文件均匹配导出 manifest。该样本只支持继续评审此序列，不是 SDK 实现或平台资格，
也不证明队列排空、延迟覆盖根因或 early-reply 的实际运行分支。

### 候选路径、回退和结果

保留现有 owner 线程、`E_BUSY`、已安装及 owner HWND 的入口检查；不增加公开参数、
权限、线程或窗口。只有初始 `GetForegroundWindow()` 恰等于 owner 时进入准备。
初始为其它窗口或 NULL/未知时完全走原唯一 NIM 路径，不尝试 SFW，不增加目标元数据、
readiness 或输入桌面的新准入条件，避免把新准备阶段的限制扩散到后台 caller。
这一初始兼容分支优先于新准备的 pending/abort 准入检查：已有 pending 只阻止 owner
前台分支的新准备，不给旧后台 NIM 增加拒绝条件；实际原 BOOL 仍决定该调用结果。

| 准备阶段的条件 | 候选动作 |
|---|---|
| 尚未调用 SFW，目标元数据无法取得；owner/前台仍安全、无取消或重建提示且预算剩余 | 跳过准备，回到唯一原 NIM |
| 已取得目标后发生身份竞态，或准备中 owner/前台失效、取消、重建提示、预算耗尽 | 保守失败，零 NIM；不能伪装为元数据不可用回退 |
| SFW 已尝试，包括其 BOOL 为 0 | 从此不再回退未准入的原 NIM；任何后续拒绝、未知或不安全条件均零 NIM |
| SFW、单次 barrier 及最终状态准入全部满足，预算仍剩余 | 执行唯一原 NIM，不重试 NIM |

`.2/.3` 的原策略要求进入准备后前台不得变成第三方或 NULL/未知；激活前要求仍为 owner，激活后最终
要求为同一目标 Shell。owner、目标 HWND/PID/TID、Shell 发现结果、进程存活及 birth、
会话和输入桌面关系均须复核。目标只在本次调用内持有最小查询/同步权限的进程句柄，
各路径都释放；不缓存 Shell HWND，不仅凭类名或文件名接受目标，不硬编码 `Default`
桌面或开发机路径。持有进程句柄不能固定外部 HWND 寿命，复核与 NIM 之间仍非原子。

原入口校验错误保留原码。准备阶段的保守拒绝为 failure / `E_TRAY_FOCUS`，message
区分准备拒绝与实际 NIM FALSE；该码不再必然表示已经调用 NIM。实际执行 NIM 后，
仍只有其原 BOOL 非零才能 success，FALSE 仍为 `E_TRAY_FOCUS`，不以 readiness、
焦点效果或重试转换成功。NIM 内重入的新提示只延后处理，不改写已实际取得的 BOOL。
`return_focus()` 不改变 `installed/desired/cleanup_pending`，不清除调用方原失败。

### 单一预算与状态谓词

准备开始、目标元数据查询之前设置一次绝对单调期限 `start + 1000ms`。元数据、身份
检查、SFW、barrier、readiness 和最终准入共用它，任何阶段不得续期。SFW 最多一次；
`SendMessageTimeoutW(target, WM_NULL, ...)` 最多一次，使用
`SMTO_NORMAL | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT`（34）和当时剩余正毫秒数，
不额外再给 1000 ms。外层返回非零才是发送成功，WM_NULL 的消息结果槽为 0 正常。

随后对已确认的目标线程执行 `GetGUIThreadInfo`：查询成功且
`hwndActive == target && hwndFocus == target` 才是该瞬间的候选 ready。
成功查询但 focus 为 NULL 是已知的未就绪状态，可在预算内等待；查询失败/未知不当作
ready，保守拒绝。一次等待请求最多 `min(5ms, 剩余正预算)`，按实际单调时间重新扣除，
不按请求值累计；不以等过固定时长代替谓词。包括最终复核在内最多 256 次状态查询，
预算或次数耗尽均零 NIM。匹配后立即再次核验身份、前台、生命周期、取消/重建标志和
ready，最后检查预算才可进入 NIM；生产路径不输出逐步日志，也不复制诊断仪器化代码。

每次元数据/状态查询、SFW、barrier、等待及最终 NIM 准入前后的有意义边界都检查
同一预算与私有取消/重建 hook。保持 NORMAL 的重入语义，不用 SMTO_BLOCK 隐藏重入，
不加手动消息泵、后台线程、AttachThreadInput、合成输入、前台权限绕过或全局 timer 设置。
同线程或输入队列关系未知时不声称 timeout 必然生效；同队列发送可能忽略 timeout，
Sleep 调度、同步 USER32/Shell 调用也不能硬抢占。返回后若预算已过，不得继续 NIM；
这是一秒 cooperative 准备预算，不是整个成员调用的一秒完成保证。

此谓词是待生产验证的私有策略，不是 Windows 对通知区焦点的通用保证。其它 Shell、
初始已 ready、始终为子窗口焦点、跨系统及输入竞争都可能得到不同状态；不能为通过
测试而把未知、任意 Shell 子窗口或有限相等采样宣称为已完成。

### NORMAL 重入中的取消与恢复提示

现有服务没有公开 cancel。准备中的嵌套 `remove()` 仍返回 `E_BUSY`，仅锁存私有
prepare-abort，不修改 desired、安装或清理状态、不执行 DELETE；外层取消后零 NIM，
真正移除仍须由应用在非忙时重试。成员未返回时仍禁止销毁服务或 owner。

owner 线程上，忙时收到已注册的 `TaskbarCreated`，仅在 desired 有效时合并为当前
身份代次的私有 pending；返回 `handled=true`、`E_BUSY`、无事件，公共状态快照不立即变化。
错误线程不得锁存；旧代次的 SDK 唤醒不得锁存为新恢复提示。
其它忙时消息保持 `handled=false/E_BUSY`。调用方只可把已注册 TaskbarCreated、
handled、无事件且 E_BUSY 的精确组合记为延后提示，不能吞掉其它失败；后续真实恢复
及焦点失败仍须检查。stale 标记唤醒仅以 handled/无事件/success 消费，不触发恢复。

外层 guard 解除 operating 后，若 owner/desired 仍有效且没有排队唤醒，最多执行一次
deferred `PostMessage`。复用原 `TaskbarCreated` 分派入口，携带私有标记和不跨身份/
对象复用的标量 token，不传指针、不注册 this 或新回调。标记不认证发送者，消息仍
只是提示。后续非忙 `dispatch_message()` 才消费 pending 并执行原 ADD/VERSION_4
恢复，返回真实恢复结果；不得在 guard、析构或 `return_focus()` 内隐藏恢复 ADD。

真实提示抢先消费时使已排队 token 失效；stale 唤醒只丢弃，不重复 ADD。
若当前有效唤醒在另一操作的重入期间被消费，须使已排队 ticket 失效、保留 pending，
不能误认为该消息仍在队列。非恢复外层 guard 才可投新的唯一 ticket。恢复操作本身
不再自动 post，即使期间又收到提示也只保留 pending，等下一次外部驱动调用/提示，
避免自投递重试链。恢复失败沿用原 desired/不确定注册/cleanup 契约，不自动循环重试。
`remove()` 先取消 pending 并使代次/token 失效，再走原清理；析构不 post、不 ADD，
仅保留原尽力 DELETE。投递失败保留 pending，不覆写原 API 结果；后续没有外部驱动
时不保证自动恢复，投递成功也不等于恢复成功。

### 实现边界、备选与后续验证

候选源码范围限于 `sdk/src/runtime/services/desktop/tray_service.cpp`、未安装的
`tray_platform.h` 和 `tests/native/tray_service_tests.cpp`；必要的私有 helper 随同审查。
typed seam 应暴露可区分失败/未知/未就绪的目标及 GUI snapshot、单调时钟、一次
activation/barrier、有界 wait 和 deferred post。生产策略直接使用该 seam，fake 验证
真实策略顺序；不能只 fake 一个“准备成功”总 bool 来绕过阶段测试。借用适配器寿命
仍长于服务。不改 menu/global-shortcut、公开签名、Web Bridge 或可信手势定义。

备选包括完全保留旧路径、只做 activation/barrier、固定 Sleep 后 NIM、准备失败一律
回原 NIM，以及新建异步服务。旧路径作为不进入准备时的兼容分支保留；barrier-only
已有键盘反例，固定 Sleep 不验证状态，SFW 后无条件回退可能重现错误路由；异步服务
扩大线程与生命周期契约，均不作为本候选接入方式。代价是 owner 线程同步等待和
前台准备失败时更保守的可用性，单样本不能消除这些风险。

后续 fake 必须覆盖：初始非 owner/NULL 跳过但原 BOOL 不变；元数据缺失回退与身份
竞态拒绝；SFW 拒绝、barrier 失败、瞬间 ready 与先 false 后 true、未知查询、ready
丢失、第三方前台、绝对预算/次数边界、等待超时实耗与时钟异常；最终 NIM 唯一且其
TRUE/FALSE 不被改写。每个重入阶段验证 `E_BUSY`、私有取消/pending 和公共快照不变，
覆盖合并、later nonbusy 恢复、post 失败、真实提示抢先、remove/reinstall/重建对象的
stale 丢弃、恢复内重入及失败无自投递循环。原所有权/清理失败测试和严格消费者断言保留。

本地 SDK 内容一旦变更即须换新身份，同步 VERSION、公共版本入口、CHANGELOG 及
API/迁移说明；不得让 `0.6.1-preview.1` 对应不同 SDK。依现有授权完成两架构契约、
独立安装/消费者构建和隔离桌面中的 API+真实菜单选择/Esc/Shift+F10 路由、取消/重建/
退出等适用验证后，方可提请公共合并并由维护者评审批准。Win7 SP1 x86 尚未验证，
不能提高最低系统来规避；完整 #27/#29、长期及生产门槛另行验收，禁止以此 probe 或
fake 测试更新通过结论。

### 2026-11-03 决策补充：激活后的 NULL 前台作为待就绪状态

外部 POD 诊断在同一 Win10 上得到两例实际失败：一次在成功 SFW 和成功 WM_NULL
后、一次在成功 SFW 后且尚未调用 WM_NULL 时，完整已知的身份/owner/桌面/session/
菜单检查通过，但原前台查询返回 NULL，触发严格前台谓词拒绝，均未调用 NIM。
诊断源码与 `.3` SDK 分开封存；这不证明 NULL 的内部产生原因，也没有复现旧
`E_MENU_FOREGROUND`。Microsoft 文档明确允许窗口失活期间返回 NULL，故不能把
这一结果直接等同于已观察到第三方前台。

计划在新的 `.4` 身份实施以下窄修，实施及验收结果另记，不追溯覆盖旧失败：

- 初始前台分支不变；初始 NULL 仍只走原 NIM。进入准备后、SFW 前仍要求前台恰为
  owner。唯一 SFW 返回 FALSE 仍直接失败；不增加激活、barrier 或 NIM 重试。
- 只有已执行且成功的 SFW 之后，在其它准入条件全部已知且安全时，NULL 前台可
  暂记为 pending，允许继续唯一 barrier 和原有界就绪等待。它永不计为 ready，也
  不授权 NIM；未知元数据、身份变化、非 NULL 第三方前台、取消、重建提示或失效
  owner 仍立即拒绝。barrier 后的非 NULL owner 前台保持原拒绝规则。
- 所有阶段继续共用原绝对 1,000 ms 预算，等待每次不超过剩余时间与 5 ms，原
  256 次就绪检查上限包含最终复核。最终仍必须实际观察前台恰为 Shell 目标，且
  GUI active/focus 均为该目标。最终复核遇到安全 pending 时回到同一有界等待，
  不把 FALSE/NULL 改成成功；预算或次数耗尽时零 NIM。
- 保留原线程、重入、Taskbar/取消、所有权和清理语义。故障注入先证实旧实现对
  激活后、barrier 后和最终复核的 NULL 立即拒绝，再覆盖 pending→ready、持续
  pending、期间第三方/未知/重建/取消以及原 NIM FALSE。真实两架构验证须绑定
  新版本、源码、载荷及新的用例身份；本补充不构成已修复或兼容性通过声明。

备选是维持 NULL 立即失败、把 NULL 当 ready、固定 Sleep 后无条件 NIM、重试
SFW 或增加前台权限。前者保留已观察到的过早拒绝，其余无法保持实际就绪和权限
边界，均不采用。代价是原本立即拒绝的 NULL 分支现在可能消耗剩余协作预算，
实际 Windows 调用仍无法硬抢占；持续失活或输入竞争仍可明确失败。

依据：[GetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow)、
[SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)、
[SendMessageTimeoutW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)、
[GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo)、
[Sleep](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleep)、
[PostMessageW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postmessagew)。
