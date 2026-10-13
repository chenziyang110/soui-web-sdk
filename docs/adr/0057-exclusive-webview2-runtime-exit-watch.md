# ADR 0057：显式独占 WebView2 配置与应用运行时退出观察

状态：已采用；共享双视图、独占冲突、重复关闭及 Modern 界面流程已有分项实际验证和独立复核。完整系统、长期及发布资格仍为 false；历史失败保留。

## 背景与事实边界

本机 x64 Reference Modern smoke 的真实根进程 PID 19760、创建 FILETIME `134360608716666359` 在 1489ms 返回 0。原监督器在 3511ms 返回 124：原两秒子进程排空窗口后仍有五个本次 Job 内的运行时进程，随后强制清理；`genericPassed=false`。应用已经报告真实 GUI 文档绑定、GUI stop、Host close、Bridge join。它们证明各自阶段，不能代替整个运行时进程组退出。旧记录 `out/rav338350a0c-x64/` 保留。仅凭五个可执行文件名不能识别实际 browser PID。

现有 `webview2_adapter::unmount()` 关闭控制器、撤销 Bridge 回调、等待自己的 worker/callback、释放控制器和环境引用。这是控制器生命周期契约。共享同一配置目录的其他 Host 必须能够继续运行，因此本次不让普通 `unmount()` 或 `webview_host::destroy()` 等待全组退出。

Microsoft 的 Environment5 事件在关联运行时进程集合退出并释放资源后发出；消费者须保留环境直到事件交付，共享目录的应用需协调。[Environment5 文档](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment5?view=webview2-1.0.3537.50)

## 决策与兼容性

新增 `webview2_profile_access::{shared,exclusive}` 和 owner STA 上的 `configure_profile_access()`，只在 created、提交 prepare 前接受。默认值仍是 shared，默认环境创建仍传空 options；共享路径不注册退出观察，也不增加最终运行时等待。未知枚举返回 `E_INVALID_PARAMS`，迟调用返回 `E_INVALID_STATE`，错误线程返回 `E_THREAD`。

exclusive 使用固定公共 `CoreWebView2EnvironmentOptions` 的 EnvironmentOptions2 属性 `ExclusiveUserDataFolderAccess`：put TRUE、get 必须 TRUE，再把 options 传给环境创建。put/get/接口转换失败保留实际 HRESULT 文本；成功但读回 FALSE 使用独立固定错误。没有共享回退、抢占已有配置目录、删除目录或自动换目录。成功读回仅是配置观察，真实环境/控制器创建成功才建立可用运行时。[EnvironmentOptions2 文档](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environmentoptions2?view=webview2-1.0.3537.50)

该属性排除其他进程使用同一 UserDataFolder，不排除本进程的多视图。Reference Modern 明确选择 exclusive，普通持久目录和 smoke 子目录均如此；路径名称不是独占证明。本轮 Reference 只创建一个适配器/控制器。应用关闭时冻结新视图和恢复；未来同进程多视图须先分别关闭所有控制器，再由一个应用级所有者完成退出观察。不得把窗口 A 的 unmount 绑到仍运行的窗口 B。

新公共类型只依赖现有 SDK、标准 C++ 类型；不公开 COM 对象、任意 callback、kill、恢复或脚本执行。现有构造函数和适配器 opaque 成员布局保持，新增符号要求 SDK 与使用新接口的消费者重编；不宣称一般旧二进制 ABI 合格。SDK 不引用 Reference 实现。

## Owning watch 与真实身份

`acquire_runtime_exit_watch(std::unique_ptr<webview2_runtime_exit_watch>& empty_output)` 导出同一个内部 owning monitor，不另建环境、任务服务或 handler。要求 owner STA、exclusive、空输出且未取出过。成功 prepare 后应用在 Host.start 前立即取得它；已有 monitor 也可在 failed/closing/closed 阶段导出用于真实清理，但不改变原 phase、失败或 readiness。

| 情况 | 返回及所有权 |
|---|---|
| shared | `E_RUNTIME_WATCH_SHARED`，没有应用级独占退出契约 |
| 输出非空或已取出 | `E_RUNTIME_WATCH_ALREADY_ACQUIRED` |
| 初始化未 settle 且尚无可导出的 monitor | `E_PENDING`，保留适配器、回调及借用所有者 |
| 已 settle 但没有可识别的 monitor | `E_RUNTIME_WATCH_UNAVAILABLE`；不能捏造 browser 身份或正常退出 |
| 已 armed，或真实 monitor 已记录失败 | 导出同一 monitor；失败不会被导出操作清除 |

环境创建后先取得 Environment5 和 canonical `IUnknown` 身份，再请求控制器。在正常控制器回调取得真实 core 后立即 arm，早于 preload/策略/Host mount。取消控制器回调若实际取得 controller，也须取得其 core 并在原 Close 前 arm；arm 失败保留真实控制器供原 unmount 清理，不把取消算成功。

arm 读实际 core 的 BrowserProcessId，打开 `PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE` 句柄，读 GetProcessTimes 的创建 FILETIME 和 QueryFullProcessImageName 的完整路径，并要求进程仍存活。注册退出事件后再次核 core PID、持有句柄创建身份和存活状态。初次及复核 WaitForSingleObject 的 WAIT_FAILED 均记录 `process_wait` 和实际 GetLastError；提前退出或重绑定是失败。持有句柄阻止 PID 复用替代这个观察目标。实际镜像散列由 Root 的运行记录独立核对，available-runtime 清单不能代替实际选择证明。

handler 只捕获 monitor 的 weak_ptr；monitor 持有 Environment5，避免环境与 handler 的强引用环。它不借用 app/Host/Bridge，不调用业务、wait、Close 或恢复。回调借用计数在同一 scalar mutex 下进出，溢出或不一致 fail-stop。sender 通过 QueryInterface 获得 canonical IUnknown 后比较，不能比较不同 COM 接口指针。事件 getter 的实际 HRESULT、真实 event PID 与 NORMAL/FAILED 种类均检查。第一错误固定保留，后续关闭或事件不会覆盖；任何异常在 COM 边界转为固定 callback_exception。

## 快照、完成与析构

`observe()` 返回 `webview2_runtime_exit_observation`：phase、固定 error enum、HRESULT、Win32 error、持有 browser PID/创建 FILETIME/image path、event PID/种类、event_observed、process_signaled、registered、active_callbacks。身份在 arm 后固定；事件/失败/计数在同锁读取。owner STA 的零等待在 scalar 锁外执行，未持有 COM 或原生调用的锁。事件与句柄观察不是一个原子操作，使用方必须保留这一边界。

phase 为 pending/complete/finished/failed。complete 必须同时满足 armed、首次匹配 NORMAL 事件、event PID 等于持有 PID、实际 held process signaled、active_callbacks=0、未 finishing、没有失败。只有事件或只有进程退出都不够。wrong-thread observe 返回固定 failed/wrong_thread 空快照，不把空字段当实际身份；moved-from 返回 failed/unavailable。正常 owner 的首因仍保留。

`finish()` 只在 owner STA 执行。pending/活跃借用返回 `E_BUSY`；永久错误返回 `E_RUNTIME_WATCH_FAILED`；moved-from 返回 `E_RUNTIME_WATCH_UNAVAILABLE`；错误线程返回 `E_THREAD`。完成后先成功 remove_BrowserProcessExited，随后检查回调借用/首因，再成功 CloseHandle，释放环境/identity，最后再次检查重入和借用才置 finished。token=0 不能解释为未注册，使用显式 registered 标志。finishing 阻止 COM Release 重入被误认为已完成。失败保留可保留的资源和所有者，不能假称优雅关闭；成功后的重复 finish 返回成功，不重新注册。

watch move/call/destruction 均要求创建它的 STA。move 仅转移同一个 monitor；覆盖未 finished 的 owning watch、错误线程移动或析构、未 finished 析构均 `std::terminate`。moved-from 析构无资源。exclusive adapter 析构要求实际 preparation settle、owner STA 及其 monitor finished；否则 fail-stop。monitor 仍持有环境、句柄、注册或 callback 时析构也 fail-stop。共享适配器原析构路径不变。

无 Core 准备失败与成功 runtime finish 分离。仅 accepted 的真实 Controller 创建回调返回失败且 controller=null、原失败为 E_WEBVIEW2、全部 preparation completion/callback borrow 已 settled，并且从未取得 Core/held browser/arm/acquired watch 的窄路径，允许 owner STA 在 unmount 中释放失败准备资源。其他没有可识别 Core 的未解决初始化/取消/同步提交失败仍不能套用此清理；没有 NORMAL/PID/signaled 观察绝不成为整个 Runtime 退出证明。

## Reference close 与观察来源

原 GUI stop → selected Agent stop → Host destroy 顺序保持。E_BUSY/E_CURSOR_CLOSE_PENDING 保留原 20ms close timer 和真实日志，未知错误仍保留非零 exit_code；先前非零退出码从不清零。Modern 单独记 controller_closed，只在真实 destroy 成功后记录一次 host-close-complete，停止普通业务 timer。随后同一 owner GetMessage 循环和 20ms close timer 驱动真正事件；保留 Host、adapter、watch、Bridge/业务 context、HWND 和 COM STA，直到 watch finish 成功再清 Agent owner 并 DestroyWindow，后续原 Host withdrawal、Bridge join、COM shutdown 顺序保持。

第一次 close 启动一个 10s 应用预算。它在后续 close 入口检查，不能抢占同步 Close/COM 调用，不能等同整体 hard deadline。超时、失败事件、身份/句柄/移除错误输出固定观察并保留 owners fail-stop，不延长原两秒外部 Owned drain，不 kill browser 或调整全局运行时。早期 prepare 失败也先走真实 Host cancel/settle，取得同一真实 monitor 或明确 unavailable；不得通过重放 action 或第二环境来补观察。

仅既有 opt-in app observations 开启时写 `runtime-exit.ndjson`，schema `soui.reference-runtime-exit/1`。它是读取实际状态的非原子诊断，不能构造授权收据、session digest 或替代外部 Owned 结果。

| 字段 | 实际来源与采样点 |
|---|---|
| schema/phase/profileAccess | 固定值；阶段只有 armed、armed-during-cleanup、finished、failed、deadline |
| pid/creationFileTime | 写入时 GetCurrentProcessId/GetProcessTimes；十进制字符串 |
| qpc/currentThread | 写入时 QueryPerformanceCounter/GetCurrentThreadId |
| ownerHwnd/ownerThread/ownerProcess/ownerValid | 所有 runtime-exit 行均在成功 DestroyWindow 前；实际自己的 HWND、GetWindowThreadProcessId、IsWindow，不查询已撤销 HWND |
| watchPhase/watchError/hresult/win32Error | 紧邻写入前实际 observe 的固定状态，不据打印阶段补成功 |
| browserPid/browserCreationFileTime/browserImage | monitor 的真实 held identity；尚未取得时 0/空表示 absence |
| eventBrowserPid/eventKind/eventObserved | 真实 handler 首次事件观察；absence 明确不是 NORMAL |
| processSignaled/registered/activeCallbacks | observe 的 held wait 与同锁状态；finished 行 registered=false/callbacks0 仍须由外部 reader 核实 |
| nonAtomic | 固定 true；QPC/窗口/进程与 monitor 不在一个原子快照 |
| exitCode | 当前 app 原 exit_code，不能恢复为 0 |
| qualificationPassed/releaseReady | 固定 false；不能授权整个 SDK/工具链/发布 |

写日志失败只导致证据缺失，不能提前 release；Root reader 须拒绝缺失记录。既有 app-observation 的 post-withdrawal 三阶段 owner=null 规则保持不变。

## 回归与迁移

原始实现候选只含三个真实生产文件的十九个 sequential byte spans 和此 ADR。其准备阶段固定 before 全散列，未安装、运行或修改实时源码；后来采用与实际验证分项记录在下表，不能把候选准备说明当作当前执行状态。普通共享消费者无需采用新 API。exclusive 消费者须重新编译、在 prepare 前明确 opt-in、先取得 owning watch、保留 STA/HWND 至真实 finish；析构不是 cancel 或 best-effort release。

`focused-tests/` 是明确 detached scalar source-body 回归：直接抽取候选的 first-failure、finished/borrow guard、phase conjunction 三段字节，独立检查首因、无 core 未完成、回调借用、NORMAL/PID/signaled/finishing 各个边界。它没有真实 COM/进程/环境调用，是边界 mock，不是运行时或故障清理资格。CMake 在 project 前及 mandatory prebuild 检查完整候选源、三个 exact ranges、fixture 和当前正常公共 headers；正常输入采用后须显式更新 pins，不能自动 bypass。三项 CTest TIMEOUT10，无 WILL_FAIL，尚未执行。

Root 实际验证至少需要：默认 shared 两视图 A 关闭而 B 有有限进展；exclusive 同一 UserDataFolder 第二真实进程被拒绝；Reference Modern 当前 SDK/载荷实际 GUI绑定→controller Close→matching NORMAL+held signaled→成功 unregister/owner撤销/Bridgejoin；prepare cancel/repeated close 的实际 callback ownership；同进程 exclusive 两视图由最后应用 owner finish。必须保留原 Owned 两秒 gate，root0/supervisor0/natural active0/no force 才是自然结果；编译 0/124 仍 generic false。

Win7/所有 OS、WebView2 全部 API 故障、no-core 恢复、完整工具链/SDK、物理 GUI/键盘/IME、性能、通用 unload 与 release 均未关闭。旧 React/Vue 成功和 Modern force-tail 失败原件不变。

## 无 Core Controller 失败的独立清理终态

真实 Controller completion 先由既有 preparation_lifetime.receive(controller) 接收；只有 accepted completion、FAILED(controller_result)、controller=null 且仍处于原非取消准备路径才记 controller_creation_failed。应用回调重入 unmount 时，preparation.settled() 仍为 false，返回 E_BUSY，不能从回调中释放环境。取消但异步创建尚未返回、同步 submission failure、实际取得任一 Core/Controller/held process、arm/acquired watch 或监视器首因失败，均不适用此窄终态。

unmount 不更改普通共享和正常 exclusive controller/watch 顺序。在准备已 settle、worker 已 quiet/join、原 Bridge 借用已撤下后，窄失败路径检查 monitor 无 armed/acquired/finished、无 process/PID/creation/image、无 event、无 active callback。失败清理中的 owner 重入返回 E_BUSY。若 registered 确实为 true，先实际 remove_BrowserProcessExited 并保留 HRESULT/borrow 失败；token 数值为零不等于无注册。真实 no-Core 路径通常从未执行 arm，因此没有注册，不能打印“已移除 token”冒充调用。所有 COM Release 在 scalar lock 外；释放后再次确认 active callback/首因，才释放 monitor 与 adapter Environment。

failed_preparation_cleanup_complete() 是 owner STA 只读布尔查询。true 仅表示上述失败初始化的 monitor/environment/controller/core/worker/Bridge 借用已释放且 adapter 已 closed；不是 runtime watch phase，不意味着 browser NORMAL/signaled 或 Runtime process group 已退出。status() 仍返回原 E_WEBVIEW2/HRESULT，prepare 回调结果不清零。成功重复 unmount 仍幂等；成功 owning watch 的原 NORMAL+held process+token removal 门完全不变。

Reference 在真实 Host.destroy 成功后，若没有可取 watch，仅 E_RUNTIME_WATCH_UNAVAILABLE 与这个只读 failed-cleanup=true 的组合允许撤下应用所有者；既有 exit_code=3 和原错误日志保留，并单独记 failed-prepare-cleanup-complete。其余 unavailable/未知失败仍 retain/fail-stop。这里没有 browser kill、未知进程等待、目录抢占、重放导航或延长原外部 Owned 2s drain。

真实 scope 回归以已持有正常 exclusive parent 和 SUSPENDED 后核实同一 current Job 的 self child，本机固定运行时 Edge WebView2 154.0.4258.62 的两架构 Controller 回调实际返回 0x8000ffff；当前有限 fixture 严格核对这个原始结果，SDK 不转换或替换错误。它不为其他运行时版本规定相同 HRESULT；其他准备失败不能算本轮固定独占冲突。child 在实际 unmount 后必须读 failed-cleanup=true、status 原错误未改、watch unavailable，再自然退出；parent 的真实 NORMAL/signaled owning watch 仍独立完成。shared 两 view 与 close-idempotent 回归保持正常公共 API。下表记录已执行的分项事实；全部原 raw/forced-tail 失败和 broader qualification=false 保留。[Microsoft 的独占冲突契约](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environmentoptions2?view=webview2-1.0.1264.42) 与 [Environment5 remove/event 契约](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment5?view=webview2-1.0.3537.50) 是此有限分离的依据。

新增只读方法保持 webview2_adapter 的公开对象布局不变；静态 SDK 与调用这个方法的消费者仍须一起重编。没有声称旧二进制可调用新符号。通用 API/OOM/跨线程错误、取消创建未完成、未知 browser 身份或全部错误回收仍未关闭。


## 当前实际验证（2026-10-10）

| 场景 | 原始汇总与 SHA256 | 有限结果 |
| --- | --- | --- |
| x64 shared 双视图 | out/ws-shared-28539b6b-x64/summary.json，40908 bytes，5ce85437d9c39952ca160c8be9b5a0e6170421368832e559ed61516b98976d63 | 关闭第一视图后第二视图继续导航；两个 owner 撤回后，本次 Job 新成员在同一原 10s 期限内排空，实际 6531ms；case 8393ms 自然 0/0、Job9 active0 |
| x64 exclusive 冲突 | out/wse-fcb77da6-x64-exclusive-conflict/summary.json，40764 bytes，584436cffbdecae3cd060a8dbea1702be3fefede532fbfc35e0e8c58e46b54fb | 真实持有 child 与当前 Job 对应，failed callback 内 unmount 为 E_BUSY；settle 后 failed cleanup=true、原错误未变、没有虚构 watch/NORMAL；case 自然 0/0、Job12 active0 |
| Win32 close-idempotent | out/wse-fcb77da6-Win32-close-idempotent/summary.json，24912 bytes，7306c5b62503f6800789f96e86c77f1c0258cbb14276453f5d8f5497ea531ad5 | 实际 browser94504/FILETIME134360730931513610 的 NORMAL、signaled、registered=false、callbacks0 在 COM 撤回前观察到；case6159ms 自然 0/0、Job8 active0 |
| Win32 exclusive 冲突 | out/wse-fcb77da6-Win32-exclusive-conflict/summary.json，44108 bytes，5900724b9927151c5dd105a0c7ba499fb29395e005677736ba7b0c31a4675b1e | held child95284/FILETIME134360732029394329 实际拒绝并完成失败清理/退出0；parent browser101872/FILETIME134360732003322643 独立完成正常 owning watch；case5739ms 自然 0/0、Job12 active0 |
| x64 Modern 完整 GUI | out/rcqueue-6a9cf247-actual/summary.json，7495 bytes，67f405449bdfc10336e508e1fe68135086ff7b97659b55b6e85736bea9d95309 | 九类 GUI family、十三次 operation 和真实子任务取消完成；browser74104/FILETIME134360710970022426 的真实 watch 完成在 Host close 后、Bridge join 前；应用/Job9 自然 0/0 |

Root 与独立审查分别核对真实 CL25/optional1、OBJ/archives、实际项目 cwd 的 LINK 与绝对 LINK.read、MAP、PE 架构、每阶段原始 Owned/context/pending/finished、原生 PID 与字符串 FILETIME、阶段顺序和本次进程身份。以上 runtime case 均无强制清理；各自 configure/build 的 root0/supervisor124 compiler partial 仍原样保留，整轮 controller1、allGenericPassed=false，不是完整工具链通过。

shared 的排空只观察本次 current Owned Job 新成员；保留创建视图前核实的 root/conhost 基线，没有查询、等待或清理其他进程，没有为 shared 建造 owning watch 或 NORMAL 事件。它不认证任意外部共享进程组的自然退出，也不覆盖 Win32 shared、同进程 exclusive 多视图、所有 API 故障、长稳、物理输入或系统矩阵。以前的 shared 过早退出导致原 2s drain 强制清理的失败原件仍为失败。
