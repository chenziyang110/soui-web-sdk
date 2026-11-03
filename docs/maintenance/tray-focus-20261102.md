# Workbench 驻留焦点失败的调用方政策（2026-11-02）

`0.6.1-preview.1` 是本地未发布候选。本次只调整 Workbench：菜单结束后精确的
`failure / E_TRAY_FOCUS` 仍记录失败并锁存退出码 1，但不立即退出，以便成功的
有效菜单命令继续原消息路由。其它焦点、菜单、清理、Host 错误和已有退出请求
保持原处理。决策见 [ADR-0013](../adr/0013-public-menu-shortcut-and-residency.md)。

## 已执行构建与身份

两架构 Release 均通过标准 `tools/Build.ps1 -SkipWeb`，复用已实际构建并由官方
`record-web` / `verify-web` 核验的四份 distribution Web 输出；未重复安装其余已成功
的 Web 工程。工具链固定为 MSVC v143 `14.44.35207` Hostx64、C++17 和 Windows SDK
`10.0.19041.0`，未提高最低 Windows 要求。四个自有 configure/build VCTIP 均自然退出 0。

- GUI 原件绑定源码 SHA-256：`120d189c6c3599ed7f2ae32dce9e410ef9ff7afec733ac622cacc492f1511670`。
- `demos/workbench/app/resident_controller.h`：36,852 字节，SHA-256 `ef63f17d3e0efa766cae9270fceb73521d9ec92274e334edbbdc326666478494`。
- 每架构完整 Workbench 运行载荷均为 671 个文件，位于 `out/<架构>/bin/Release/SouiWorkbench`。

| 身份 | x64 Release | x86 Release |
|---|---|---|
| Build ID | `8c618775-a7f5-4bdc-95fb-74008ef5e3d5` | `2a0a5ade-e4a9-4d22-9fa2-1cb2481016cf` |
| EXE 字节数 | 1,719,808 | 1,509,888 |
| EXE SHA-256 | `8a356517320c15d0a2fd66ef3ef3e53296218940864b85e701030531471b222b` | `512d0770e6a28176a7027d64fee14b122291d155c3f1044df57e7d2f83a30497` |
| 完整载荷 SHA-256 | `f20cf2bd7187d240e849e248163bcfd1ed321e691e8b9e3b6d391cf6e65e54e1` | `847f6a03e1403f6db018fc62d8986c751594aa9cb54ab038fd3707bac260577e` |

每架构实际运行 14 项非 GUI CTest，全部退出 0：`tray-service` 14 组、`menu_service`
15 组、`global_shortcut` 9 组，以及十一项纯 `resident-shutdown-policy`，合计 49 个
契约组。它们不启动产品窗口，不替代真实桌面验证。未执行无筛选的 `Test.ps1 -UnitOnly`。

首次 Workbench `npm ci` 网络中断及两架构首次旧 VCTIP 门禁拒绝均保留；后续恢复
没有删除缓存、放宽锁文件或关闭门禁。SDK 与 Workbench 的最终 npm audit 元数据
均为 0 个漏洞；此前 TLS 连接失败原件保留，不将失败响应当成零漏洞结果。

上表引用 GUI 当时的冻结身份。Workbench README 属于构建源码清单；文档更新后已
通过官方 Web 记录、两架构标准增量构建和各 14 项限定 CTest，最终源码 SHA-256 为
`573b5a7d7e22813fc9075b550302bf193fda9558a417f246a43d7c437c96f695`。
x64 新 Build ID 为 `7b6c214b-fbe8-43f5-b06c-761a5c2597d3`，x86 为
`bb922690-c129-44d1-af29-b07fc7710940`。源码清单差异只有 README；每架构全部
671 个运行文件均与 GUI 冻结产物逐字节相同，原 EXE、DLL、资源及载荷散列保持。
旧构建记录、源码身份和 raw 永久保留，不作追溯刷新；最终重建不替换真实 GUI 原件。

## 两个完成流程的实际样本

在隔离 Server 2022 VM 中执行，时间为各自 resident 进程起点后的毫秒；Owned 使用
独立起点，监督预算保持 200,000 ms、drain 2,000 ms。

| 事件 | x64 `combined-x64` | x86 `rerun-x86` |
|---|---|---|
| 菜单命令 3 成功返回 | 67,797 | 157,265 |
| 严格焦点失败及 warning，锁存 1 | 67,813 | 157,265 |
| 槽位 3 代际 2 创建 / ready | 67,813 / 69,953 | 157,281 / 160,437 |
| 真实 Esc 与无选择返回 | 98,719；warning 仍锁存 1，无命令派发并继续驻留 | 未执行 Esc |
| 菜单命令 11 / 显式退出请求 | 122,453 / 122,469，退出码 1 | 179,968 / 179,984，退出码 1 |
| 清理完成 / ordered resident 结束 | 123,578 / 123,578 | 180,906 / 180,921 |
| 原生 `process.finished` | 123,594，退出码 1 | 180,921，退出码 1 |
| Owned root 退出 / terminal | 123,796 / 123,808 | 181,171 / 181,179 |

两例均完成 Runtime、窗口及控制器的有序收尾，外部 Job 活跃数为 0，无超时且未强清；
严格原生结果和 Owned `passed=false` 保留。命令继续的依据包含实际新代际创建及
Host ready，不能只用 `commandFlowContinues=true` 代替。x64 的 Esc 由实际操作观察
与配对日志共同记录，单独的 `hasSelection=false` 不证明真实 Esc 或菜单成功显示。

| 原始日志 | SHA-256 |
|---|---|
| x64 完成流程 resident | `6e0787d575ec9ed2aaf6efb52d2137bf051c574cfbca6ee55af29c1df4153833` |
| x64 完成流程 Owned | `e1eeb264559248e25415617480b09e1e60d40a2391eb95910352f9cfede8112f` |
| x86 完成流程 resident | `ecc615432173190945caf13706c6c6843d18c758bc35b758f72ddda2aa21fc99` |
| x86 完成流程 Owned | `798a387d4360a96635cbb8870d188d0cf71f8d131c211927951d2590f589ec06` |

完整本地产品记录为五个 case、十份原始 NDJSON，保存在 `out/tray-focus-20261102`；
包括载体、会话、EXE、载荷、截图与原件散列，和原生诊断分别计数。三个较早样本为：

| 早期 case | 保留结果 |
|---|---|
| `first-x64` | 操作流程未完成，Owned 200,033 ms 超时，root 124，强制清理所属 Job |
| `first-x86` | 关闭槽位 3 后未完成菜单流程，Owned 200,024 ms 超时，root 124，强制清理所属 Job |
| `rerun-x64` | 未完成同一例命令选择和显式退出，Owned 200,029 ms 超时，root 124，强制清理所属 Job |

这些是操作期限内流程未完成的真实失败，没有产品自然清理证明；保留失败，但不据此
归因为产品菜单或关闭故障。后续两个完成样本不抹除它们。

## 尚未完成的范围

原生独立控制组当前共二十一个 case、四十五份 raw：十四次 `NIM_SETFOCUS` 已返回
FALSE、三次已返回 TRUE；两组未调用超时、两组在结果前异常退出分别保留，未知
BOOL 不记为成功。API 已返回 TRUE 不表示整个 case 通过；键盘观察 B 的后续期限
失败不归入未调用超时，详见下文。
修正独立诊断的 DR7 固定状态位匹配后，在固定 Server 2022 Shell32 样本中实际采到
`SendMessageTimeoutW` 返回寄存器为 1、接收结果槽为 0，随后原 `Shell_NotifyIconW`
返回 FALSE。两个采样点、原 DR 恢复与 readback、五次观察及清理均完成；原生自然
退出 1，Owned Job 活跃数 0、无超时或强清，`passed=false` 保留。

新增发送前 wire 样本的三个采样点完整，原始文件与导出 manifest 散列匹配。
`WM_COPYDATA` 的 tag=1、cbData=1484，发送 flags=11、timeout=7000 ms；载荷
magic=`0x34753423`、operation=3、wire cbSize=956，owner/WParam 与已记录窗口一致，
图标 ID=19。离线已知字段检查均为 true，但接收分支和 receiver manager 状态仍未知。
发送后 RAX=1、接收结果槽=0，原 API BOOL=0；原生退出 1、Owned `root-failed`、
`passed=false`，Job 活跃数 0、无超时且未强清。解码记录位于
`out/tray-focus-20261102/raw-wire-input/wire-decoded.actual.json`，不能把输入字段吻合
写成接收分支已证实或 API 已成功；旧未调用超时和返回前致命异常均保留。

精确 Explorer 的补充静态复核还发现条件链：托盘激活 → `StopInputObservation` →
`InSendMessage` → `ReplyMessage(0)`，随后接收线程继续工作。因此最终 WndProc
返回 1 不能排除发送方提前取得 0；本次实际是否走过该链仍未证明。该机制符合
[ReplyMessage 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-replymessage)，
不支持把现有 BOOL 0 归因为已证实的输入拒绝，也不改变公共 API 的严格失败结果。

新增现有 Win10 客户端 VM 的普通 API 对照，使用同一冻结 API-binding baseline
（EXE SHA-256 `befe8db32f8fc6f8904e58a78ab1d7140d43a5a9a10bf9868dab1913312c6d04`）。
正常自动登录后实际从来宾 E 盘载体运行，由操作者观察真实菜单和 Esc；旧 D 盘载体
未执行，不计样本。原件于 35,162 ms 记录一次 `NIM_SETFOCUS` FALSE，36,375 ms
原生退出 1，`invalid=false`、`deadlineReached=false`、`cleanupConfirmed=true`。
五次观察的实际偏移为 1/135/154/202/1003 ms，不能写成计划采样点。Owned 于
36,864 ms 观察 root exit 1，36,873 ms terminal `root-failed`，Job 活跃数 0、
无超时且未强清，`passed=false` 和晚期 `imageReadError=5` 均保留。精确 OS edition/build
未独立核实；旧 manifest 下 DLL 自报的 `6.2.19041.2788` 仅是版本视图，不是 OS 版本。
该客户端切片与此前 Server 普通 API 样本均为 FALSE，不证明 early-reply 的实际运行分支、
真实键盘焦点资格或系统级根因；原生日志也未认证 Esc 手势。两份新增原件与导出
manifest 的散列已匹配，记录位于 `out/tray-focus-20261102/raw-win10-baseline`。

该仪器化样本把失败收窄到 Shell 接收返回值，不能据此确认具体 Explorer 内部函数、
所有系统版本的根因、焦点效果或零时序扰动。marker 的 RAX 不是 API BOOL，
诊断 LastError 不是原发送错误；正常保留的 0 字节拒绝文件也不是有效拒绝记录。
该独立诊断不修改公共 SDK 或系统代码，早期两次致命异常仍保留。

新增独立预激活 A/B 对照使用同一冻结 EXE，SHA-256 为
`26acc8b2d70432a4b471075322c333e8c6d34f2f50191610bbcd2ac5691a6ec6`。
该轮结束时的历史汇总为十八个 case、三十九份 raw，十三次 API FALSE、一次 API TRUE，
另有两组未调用超时和两组返回前异常；后续样本不追溯改写该时点。
四份原始 NDJSON 及两份模块文件的字节数、SHA-256 均与导出 manifest 的六项记录
匹配；Shell32 与 Explorer 的模块散列也与旧样本相同。该 VM 的 Settings About
实测为 Windows Server 2022 Standard Evaluation、21H2、OS build `20348.587`；
模块版本 `20348.558` 不是 OS build。记录位于
`out/tray-focus-20261102/raw-preactivate-ab`。

A 不执行准备，唯一一次原 `NIM_SETFOCUS` 返回 FALSE，原生退出 1，Owned 自然
root exit 1、`root-failed`、`passed=false`。B 的四次准备检查均通过；一次
`SetForegroundWindow` 返回 1，一次 `SendMessageTimeoutW(WM_NULL)` 返回 1、消息结果槽为 0，flags=34、
timeout=1000 ms；随后唯一一次原 `NIM_SETFOCUS` 返回 TRUE，原生退出 0，Owned
自然 root exit 0、`completed`、`passed=true`。两例均完成五次观察，
`invalid=false`、`deadlineReached=false`、`cleanupConfirmed=true`，Job 活跃数 0，
无超时且未强清。B 的 Owned 通过仅属于该独立诊断，不提升 SDK 或产品资格。

焦点端点仍有关键差异：A 的首次观察为 `Shell_TrayWnd`，后四点为
`ToolbarWindow32`；B 的五点始终为 `Shell_TrayWnd` 本身。因此 B 只证明此样本的
API 确认改善，尚未证明实际托盘图标的键盘路由或 early-reply 的运行分支；
该轮尚未执行后续真实键盘观察，公共 SDK 未修改，不能据此采纳准备策略或关闭问题。

后续键盘观察 A/B 使用同一新冻结 EXE，SHA-256 为
`2fce6a6a74e8baf68bbf6daa285eb203e94c9a55571c029d0d16e764d12a3f65`，
并在相同 Shift+F10 操作条件下对照。四份 raw 和两份模块文件均与导出 manifest 的
六项字节数及散列记录匹配，记录位于 `out/tray-focus-20261102/raw-preactivate-keyboard`。

键盘 A 不执行准备，唯一 NIM 返回 FALSE；五次观察为 `Shell_TrayWnd` @0 ms，随后
`ToolbarWindow32` @20/50/207/1003 ms。第二菜单的 `keyboard.candidate` 于 30,956 ms
收到，先于 30,958 ms 的第二菜单 owner 激活；操作者实际看到 SECOND MENU 并以
Esc 关闭，截图与 raw 共同支持该次图标菜单请求路由，原生通知自身仍不认证输入来源。
原生于 41,878 ms 退出 1，`invalid=false`、`deadlineReached=false`、
`cleanupConfirmed=true`；Owned 自然 root exit 1、Job 活跃数 0，无超时且未强清，
`passed=false` 保留，不把实际菜单路由写成 API 成功。

键盘 B 准备有效，唯一 NIM 已返回 TRUE，并完成五次观察：焦点由
`ToolbarWindow32` @0 ms 转为 `Shell_TrayWnd` @19/65/205/1002 ms。实际 Shift+F10
显示的是任务栏通用菜单，没有第二菜单 candidate。原生在原有 60 秒总期限后于
60,013 ms 退出 2，`invalid=true`、`deadlineReached=true`、`cleanupConfirmed=true`；
Owned 自然 root exit 2、Job 活跃数 0，未触发 Owned 超时且未强清，`passed=false`。
这是 API 已返回后的键盘观察失败，不是未调用 API 的超时，也不能计为通过。

该反例表明预激活后的 API TRUE 不能替代真实图标键盘路由；仅有
`SetForegroundWindow` + `WM_NULL` 的准备方案不采纳到 SDK。该反例封存时公共 SDK、
`VERSION` 和严格托盘消费者均未修改；early-reply 实际执行与延迟焦点覆盖机制均未证实。

随后只运行一次新增 `activate-sync-state` 键盘对照，没有重跑旧 A/B。冻结程序为
286,720 bytes，SHA-256 `5c49195e35f661616591d36686277da7a537aba3cda8df699f2ec18f39ebc980`；
源码九项和编译产物六十二项均与冻结清单吻合。该模式将 metadata、一次 SFW、一次
flags=34 的 WM_NULL 和只读状态观察放在同一绝对 1,000 ms 准备预算内，保留原
60 秒总期限及 Owned 90 秒/2 秒 drain；最终日志写入之后再核对身份、前台、预算和
`GetGUIThreadInfo` 的 active=focus=同一 `Shell_TrayWnd`，成功路径不再写盘才进入
唯一一次原始 NIM。该瞬时条件不证明激活或队列工作全部完成。

本次初始准备样本在 +1 ms 为 active=Shell、focus=NULL，未满足条件；一次 5 ms
协作等待请求实际耗时 15 ms，+17 ms 首次匹配，最终查询和实际准入也在 +17 ms。
这里的等待依赖状态变化，不能归纳为固定等待 5 ms 即可修复。原始 NIM 于
17,558 ms 返回 TRUE，五次实际观察偏移 0/28/59/200/1012 ms 均为
`ToolbarWindow32`。操作者第一 Esc 后未以鼠标、Win+B 或 Tab 重新定位，真实
Shift+F10 打开可辨识的 SECOND MENU；原件中的 `keyboard.candidate` 在 29,599 ms
收到，先于 29,602 ms 的第二菜单 owner 激活，候选前焦点仍为 Toolbar。第二 Esc
对应 command=0/error=0，键盘观察于 41,020 ms 完成；原生 41,030 ms 自然退出 0，
`invalid=false`、`deadlineReached=false`、`cleanupConfirmed=true`。Owned 于
41,102 ms 完成，root exit 0、Job 活跃数 0，无超时或强清，`passed=true`。

新增两份 raw 及两份授权模块文件的长度和散列均匹配导出 manifest，位于
`out/tray-focus-20261102/raw-state-admission`；独立报告与原始记录、操作日志及截图
交叉核对。这是该新模式首个同时满足 API 与真实图标键盘链的原生样本；原生日志
自身仍不认证输入来源，单次正例不能证明条件充分、early-reply 根因、SDK 修复或
跨平台资格。准备耗时 17 ms 与收尾记录年龄 23,490 ms 分别保留，不混为预算超限。
SDK 接入先更新 [ADR-0012](../adr/0012-public-tray-service.md)，再验证重入、失败边界、
安装后消费者及实际产品流程。后续本地内容采用独立 `0.6.1-preview.2` 身份推进实现，
其后实际验证见下文，不继承上述 `.1` 或独立 probe 结果。

## preview.2 安装消费与真实桌面记录

该候选的标准源码绑定为 `2be49859ec60170d5e8a04f83b2dea3f6c250a5b6ea3cccc222b0348daaa6a47`。
MSVC v143 14.44.35207、SDK19041、Release 的 x64/x86 全根标准构建和安装实际通过；
BuildID 分别为 `b21c439e-78a8-43f4-8e91-d4a31f8484fd`、
`d4133681-3ad8-4f56-9126-d7e0af3b090e`。每架构 14 项非 GUI CTest、70 组行为回归
通过（tray 35、menu 15、shortcut 9、shutdown 11）。Web client 31、SDK package
15、GUI bindings 30 项通过，四个 Web/npm 工程 audit 均为 0。原生新增回归先取得
旧直接 NIM 路径的失败，再取得 35 组通过；一次测试空指针推导编译失败保留，修正
仅在测试的显式指针类型。没有在宿主运行 GUI 产品或诊断。

仓库外消费者只从安装前缀引用 SDK，完整头文件版本为 `.2`、Release/MD，安装库
逐字节匹配当前标准构建。两架构消费者和原生 stdout 捕获启动器均实际编译通过，
四个编译阶段及 Owned 均自然退出 0、Job0、无强清。首次 x64 attempt 被既存编译
进程门禁拒绝，未进入 configure；当时具体 PID 未获证实，原失败和零字节排查记录
保留。全新 attempt 通过，未修改门禁、超时或源码，也没有据后一次成功改写原因。
构建汇总为 `out/tray-focus-20261102/consumer-build-preview2.actual.json`，
SHA-256 `f77e68ad4c43b91e5bbdf258885d3b98cb8613966ce9aee6eff911e9704bb922`。

真实 GUI 使用只读 CD 和唯一 case；消费者原生期限 180,000 ms，Owned 200,000 ms
及 2,000 ms drain 保持。捕获启动器使用固定 consumer/Runner 的大小和 SHA pins，
只传入一次性 token，分别保留 stdout、stderr、启动/完成记录和原 Owned 报告。
启动器完成记录不自证其自身最终退出或最后一次 flush，消费者结论须结合全部原件。

| 目标 / case | 实际观察与原结果 |
|---|---|
| Server 第一轮 Workbench x64 | 命令 3 后 slot3 generation2 ready；未完成 Esc/键盘/退出，Owned 200,034 ms timeout/root124、强清所属 Job，失败保留 |
| Server 第一轮 Workbench x86 | 关闭后通知区激活恢复第三窗；真实 Esc、原位 Shift+F10 应用菜单、明确退出；两次 focus 响应成功，native0，Owned0/Job0，无超时或强清 |
| Server 严格消费者 x64 | menu0→1→2，更新提示和 Ctrl+Alt+F9；三次 focus 成功，native0@61,485 ms、cleanupConfirmed，Owned0@61,577 ms/Job0 |
| Server 严格消费者 x86 | 同样完成真实 Esc/键盘、更新、快捷键、退出；三次 focus 成功，native0@46,208 ms，Owned0@46,247 ms/Job0 |
| Server 新 Workbench x64 | command3→slot3 generation2 ready@50,656 ms；真实 Esc/Shift+F10、command11退出；三次 focus 成功，但 seq65 非延后 E_BUSY 锁存 native1@120,219 ms，Owned root-failed1@120,350 ms/Job0，无强清 |
| Win10 严格消费者 x64 | 首 Esc/Shift+F10 实际 SDK 菜单；三次 focus 成功，seq17 E_BUSY，更新后紧接快捷键的第三菜单 E_MENU_FOREGROUND；native1@47,056 ms，Owned1@47,511 ms/Job0，无强清 |
| Win10 严格消费者 x86 | Esc/Shift+F10、更新提示、快捷键和明确退出完成；三次 focus 成功，但 seq19 E_BUSY 锁存 native1@63,548 ms，Owned1@64,115 ms/Job0，无强清 |

Server 为前述同一已核对 Server 2022 overlay；客户端本会话 Settings About 实际核对
为 Windows 10 Pro、22H2、OS build `19045.2965`、64-bit OS。该新观察不追溯改变
旧 baseline 当时未核实 OS 版本的记录。About 截图含设备标识，仅本地保留，不发布。

七个 `.2` case 共二十六份 guest raw，和独立 probe 的二十一个 case/四十五份 raw
分别计数。十八次公共 focus 响应均成功；这些日志没有单列原生 BOOL，只有按冻结
SDK 成功路径作出的 NIM 非零推断，不能写成直接仪器化采样。真实 Esc 和无鼠标、
Win+B、Tab 重新定位的 Shift+F10 由操作日志与截图共同支持，原生通知自身不认证手势。

原件位于 `out/tray-focus-20261102/raw-workbench-preview2`（四份）、
`raw-sdk2-server`（十二份）、`raw-sdk2-win10`（十份）。每次在同一 VM 正常 ACPI
退出、QEMU0、写入句柄释放后只读导出，长度与 SHA-256 全部核对，失败不被重跑覆盖。
Server 本轮汇总 SHA-256 为
`7759c6e6a5afde529c56d889c7595d00dd2eb61ef9c9a349895da864d0b99c0e`；
Win10 汇总为 `42998aebb2a25b2a1811fdc8bbe4a0c1027549996fb3428a95bf98e0d7b3741b`。

新失败收窄到调用方在 tray 成员调用尚未返回时再次分派 callback。`notification=0`
是 SDK 的 `tray_event::none`，原 wParam/lParam 尚未记录，不能猜成具体 Win32 通知。
busy 返回发生在通知解码前，所以也不能据此断言原通知不可操作。TaskbarCreated 的
精确延后组合不适用于这些 callback；后续修复必须保留 SDK 严格失败语义，串行分派
原通知并记录实际重放结果。Win10 x64 菜单激活失败另行保留，不归因为 NIM FALSE。

Win7 SP1 OS32 仍无可操作的现存目标。进一步定向复核的历史记录仍指向已缺失的
旧 D 盘；相关 ZIP 无 OS/VM 镜像，现存两套 VMware Win7 为 x64，两支 SOUI stable
配置均为 Server 2022。没有接管他人租约、重置密码、克隆或重装来替代资格。
因此 #27/#29、Win7、Explorer 重启、长期驻留及生产资格继续开放；没有发布、
完整 Collector 或 `releaseReady` 变更。后续说明文档不改写本轮冻结构建与失败原件。

## preview.3 调用方队列与本轮验证（2026-11-03）

新候选为 `0.6.1-preview.3`，冻结标准源码为
`d5a7ad96c2213d31ab678c0fb1444ed7ed68ae96e6d264bbd6c934ec467de0dc`，
共 2,383 文件、178,214,066 规范化字节。两调用方各自持有容量 64 的 callback FIFO，
只在实际 tray 成员栈内保存原参数；既有 timer 的安全边界每轮最多重放八项。
入队不调用 SDK、不制造成功 response；TaskbarCreated 立即分派，退出撤销待处理项目。
FIFO 仅保证已入队子序列，不承诺所有模态输入的全局顺序。SDK tray 实现、私有
platform 与公共 tray 头保持 `.2` 字节不变，真实失败仍严格处理。

固定 MSVC v143 `14.44.35207` / SDK `10.0.19041.0` 的 x64、x86 Release 全根标准
构建与安装实际通过，BuildID 分别为 `b9f9f8e0-e305-4697-bc36-d9d11f738a5a`、
`c9bba0bb-c2c7-4934-bcc0-2a141ab89133`。每架构 14 项非 GUI CTest、76 组行为测试
通过（tray 41、menu 15、shortcut 9、shutdown 11）；新增六组回归对两个真实调用方
helper 验证容量、参数宽度、顺序、批次、重入、退出、实际 SDK busy 后重放、NIM FALSE
及 Taskbar/owner 失败边界。全部保留原失败断言，未在宿主运行 GUI 产品。

两架构仓库外 Release/MD 消费者只引用各自安装 SDK，连同应用自有的第四份辅助头
复制后实际构建，安装库匹配当前标准构建。每架构 671 项运行载荷和 467 项安装文件
均完成闭集、字节和散列复核。Web client 31、SDK package 15、GUI bindings 32 项
实际通过，四个 npm 工程本轮 audit 均为 0。原生八个编译阶段均自然退出 0，编译
生命周期门禁和原监督预算保持；未改依赖。标准构建与消费者汇总分别位于
`out/tray-focus-20261102/workbench-build-preview3.actual.json`（SHA-256
`a763106d86ccead35ea1b2667259ceb69b8e5723596a3cf1c88ff1253d8214e5`）和
`consumer-build-preview3.actual.json`（`9b10a9cfd5b34f21f641d7178f7faf4448929726710ceb3f9b84c7d1ae51eeb7`）。

真实 GUI 载体包含六个唯一 case，ISO 116,383,744 bytes，SHA-256 为
`c9784775366d121d694a14a1cf675ddddaccc833fd133109d920c4d9bbfd5d14`；两命名空间各
1,463 文件/296 目录全字节回读，六个原生 LNK 的目标、参数、工作目录和显示方式
逐项核对。每例只启动一次，不覆盖旧 case；原生消费者 180,000 ms、Owned 200,000 ms
和 drain 2,000 ms 保持。捕获目录的 `Preview2` 只是冻结工具的输出命名空间，实际
SDK 身份由新 token、`.3` 原生日志、EXE/库及源码绑定证明，不沿用 `.2` 应用资格。

Server 仍为同一 Server 2022 Standard Evaluation / 21H2 / OS build `20348.587`。
两消费者均实际完成：托盘菜单、Esc、原位等待至少 2,200 ms 后 Shift+F10 重开 SDK
菜单、更新提示、在菜单返回且取得新界面状态后 Ctrl+Alt+F9、明确选择退出。两
Workbench 均实际关闭第三窗、菜单命令 3 恢复新代际 ready、真实 Esc/原位键盘菜单、
命令 11 显式退出。手势由操作日志和截图支持，原生通知不自证输入来源。

| Server case | native 自然完成 | Owned completed / Job0 | focus |
|---|---|---|---|
| consumer x64 / `190ae4c390264b47b0de3b91da913d79` | exit0 @69,733 ms，cleanupConfirmed | exit0 @69,785 ms，无强清 | 3/3 success |
| consumer x86 / `8c69afab89e7411188aa64f5517283b0` | exit0 @44,441 ms，cleanupConfirmed | exit0 @44,521 ms，无强清 | 3/3 success |
| Workbench x64 / `swp3-45d72366389245f1b9eb7fba59953a80-x64` | exit0 @91,922 ms | exit0 @92,048 ms，无强清 | 3/3 success |
| Workbench x86 / `swp3-e41e24c0aafc4bc78c4834756def72c3-x86` | exit0 @78,844 ms | exit0 @78,992 ms，无强清 | 3/3 success |

Server 四例没有 callback 入队或 replayed=true；本轮未复现 E_BUSY，不能据此声称
真实重放路径已被触发或因果已普遍证实。两 Workbench 的 cursor close pending 分别
40/50 条、runtime shutdown pending 各三条，原冲突探测错误 1409 也保留；最终自然
完成不改写这些中间结果。十二次公共 focus 响应均成功，但 raw 未单列原生 NIM BOOL，
只能按冻结 SDK 成功路径推断非零，不能写成直接 BOOL 仪器采样。

Server 正常 ACPI 退出 0、全局 QEMU0、写入句柄释放后，只读精确导出十四份原件至
`out/tray-focus-20261102/raw-sdk3-server`；全部长度/SHA-256 与 manifest 匹配，
只读句柄已关闭、磁盘 metadata 未变化。两捕获完成记录的 setup、runner wait/exit0、
stdout/stderr flush 均有原记录，stderr 为零字节；helper 最终写入/退出仍不能由自身
完成记录自证。第一消费者退出后的迟到桌面观察不作为其原生运行时长。

Win10 使用同一已核对 Windows 10 Pro / 22H2 / OS build `19045.2965` / OS64bit 的
原 overlay，正常登录后本会话实际确认光驱 E，再换入同一 `.3` ISO。未改原 CPU、
TCG、内存、磁盘、父盘或网络隔离。两例各启动一次原生 LNK，完成与 Server 消费者
相同的真实菜单/取消/原位键盘重开、更新、菜单返回后快捷键及显式退出。

| Win10 case | native 自然完成 | Owned completed / Job0 | focus |
|---|---|---|---|
| consumer x64 / `65218db954db409e8a11a15c66505400` | exit0 @55,652 ms，cleanupConfirmed | exit0 @56,217 ms，无强清 | 3/3 success |
| consumer x86 / `63ec74a6160b45d0a8e6930238400c05` | exit0 @55,855 ms，cleanupConfirmed | exit0 @56,318 ms，无强清 | 3/3 success |

两份 stdout 各 46 events、16 条实际 callback，ordinal 1–16 均为 direct，
queued/replayed 为零；未见 E_BUSY、E_MENU_FOREGROUND 或队列溢出，原冲突探测 1409
保留。菜单选择均为 0→1→2，tooltip update1、shortcut1、明确 consumer-menu exit；
退出撤销的 discarded=0。两 capture 的实际 Runner exit0 和 stream flush 记录完整，
stderr 均零字节；helper 自身最终退出仍不以 proposedHelperExitCode 自证。

Win10 正常 ACPI 退出 0、全局 QEMU0 后，精确只读导出十份原件至
`out/tray-focus-20261102/raw-sdk3-win10`，全部 bytes/SHA/readback 匹配、只读句柄已
关闭，磁盘 metadata 未改变。两台原 VM 均已正常释放，未启动后续 VM。

本轮 `.3` 共六例、二十四份 guest raw、十八次公共 focus success，与 `.2` 七例/
二十六份及独立 probe 二十一例/四十五份分开计数。六例均 native0、Owned completed0/
Job0，无超时、强清；真实 FIFO 重放仍未覆盖，不能以本轮未复现 busy 证明一般因果。
原件不单列 NIM BOOL，按 SDK 严格成功路径的非零推断与实际手势证据分别记录。

Server 和 Win10 export manifest 的 SHA-256 分别为
`da5892a899f2e72916823da7d27751902c64f7ac82376214e06763a71ccbfece`、
`b788d574c798ce9a0c7e546b26448616689f0c29bb3214b5ec441266cab3ffc7`。
Root 最终只读核验含 103 项断言全部通过，冻结源码及十七份已暂存源码均匹配，
2,383 项源码均在 Git 索引中，包含两个新辅助头。该报告位于
`out/tray-focus-20261102/sdk3-root-final-review.actual.json`，46,045 bytes，SHA-256
`e3ffceb70cef4b50230ab62a8c5ea8e119cacdf7a78a6761d38383d48a012022`。
另两份只读独立 GUI 复核分别完成 5,429 / 3,731 项检查，报告 SHA-256 为
`d371d867a976300f5e939ea5d4f4588ad04a612fd1b923e1f4addb41547c3fb1`、
`0b1b23f093b99bad2112f4168086be2735f7eb622814da34b9492c8cddf4d61b`；均保留
真实队列未覆盖、非直接 BOOL 采样、helper 最终退出未知及旧快速前台拒绝的边界。
后续只更新不参与该源码身份的维护文档和 CHANGELOG，不刷新旧构建或 raw。

`.2` 的全部 E_BUSY、快速更新后快捷键 E_MENU_FOREGROUND、超时和非零退出保持原件；
本轮先确认更新菜单返回再使用快捷键，不证明原快速序列已修复，当前策略不改变
Windows 前台权限。Win7 SP1 OS32、Explorer 重启、长期驻留及完整 #27/#29 资格继续
开放，未执行完整 Collector、发布或提升 `releaseReady`。

## preview.3 追加 Explorer 重启恢复

焦点短验之后，按 #27 的明确验收范围在同一原 Server 追加两例恢复观察。SDK、
消费者、库、BuildID 和冻结源码 `d5a7ad96…` 均不变，没有重新编译或修改产品。
新只读载体仅复用已封存的两套 55 文件，加两个新 token 的原生 LNK 及 PLAN；
ISO 7,849,984 bytes，SHA-256 `4674ae3146ce48b42010dc4a270f443ed0cc58597badf350e82e5fd16aea281a`，
两命名空间各 113 文件/2 目录完成闭集及全字节回读。新 PLAN 为 6,978 bytes，SHA-256
`f5503d9d127e0c712c84f1a014959974d3e7f0117be1a7d066b0e2b67b541d5d`。

每例只启动一次消费者，再通过正常 Task Manager 选中 Windows Explorer 并点击
Restart。实际观察旧 Explorer 行及 CD 窗口消失、任务栏重建、消费者图标在隐藏区
重现；之后完成公共菜单 Esc、至少 2,200 ms 不重定位后的 Shift+F10 应用菜单、
tooltip update1、新界面状态后的 Ctrl+Alt+F9 第三菜单和明确退出。x64 两次误开的
通用任务栏菜单在操作记录中排除，不作为焦点或恢复证据。两次真实键盘菜单均有
截图，原生无选择返回不自行认证 Esc。

| case / token | Taskbar 观察 | native / Owned completed | 后续 focus |
|---|---|---|---|
| x64 / `2820ebf2c388429bb12abf416c618f93` | 1 次 @31,241 ms | native0 @161,145 ms；Owned0 @161,179 ms | 3/3 success |
| x86 / `0c6d2ec0a44f4d20a675d8bbc4b4b4bb` | 1 次 @31,388 ms | native0 @134,690 ms；Owned0 @134,754 ms | 3/3 success |

两次 Taskbar 原响应均为 handled/ok、非 deferred、installed/desired=true、
cleanupPending=false，sourceVerified=false 保留。公共 Taskbar 消息也供私有唤醒
使用，raw 未记录其 wParam/lParam，不能把广播次数单独当成真实 Restart 或 ADD 次数。
恢复判断同时依赖实际 GUI 操作、图标重现及后续服务可用。任意非延后失败仍按原
策略失败，本次唯一 ok=false 为预期 shortcut conflict1409。

两例均 cleanupConfirmed、菜单选择 0→1→2、shortcut1、退出 discarded0；callback
分别 10/11 条，全部 direct，仍无真实 FIFO 入队或重放。原 180,000/200,000/2,000 ms
预算保持，最终 Job0、closureConfirmed、无超时、限制终止或强清；x86 中间 conhost
active1 后自然归零的记录保留。x64 操作记录的 184 秒退出后观察不冒充原生运行时长。
六次 focus success 仍仅按冻结严格成功路径推断 NIM 非零，没有新增直接 BOOL 采样。

同一 VM 正常 ACPI 退出 0、全局 QEMU0、写入句柄释放后，精确只读导出十份原件至
`out/tray-focus-20261102/raw-sdk3-explorer-recovery`，全部 bytes/SHA/readback 匹配，
只读句柄关闭、磁盘 metadata 不变，并已正常释放原 VM。manifest 6,310 bytes，
SHA-256 `405e1a95137d1898e54e729812200dd64185b5a8150524c7004ed04a21ab78ce`；
运行汇总 74,885 bytes，SHA-256 `6d4e5e7fcfb60cccc7e3f4eebdb56cc5fca55a67425771b29bfc96224b841200`。
Root 新增 25 项只读断言通过，报告
`out/tray-focus-20261102/root-explorer-recovery-review.actual.json` 为 15,316 bytes，
SHA-256 `f18ec7b6925ec76b9efd9072b90e6a2b66da0e1d3723ab3ae7f6a6f23d11ef0c`。
独立复核 3,873 项检查通过，报告 SHA-256 为
`103724e82937dd52abf474cf6764913d52b1cb0185a9aa35602e11b229f6cad9`，
包含十七张本例图的 pins 及十一张关键图的独立查看，保持相同事实边界。

`.3` 现在合计八例/三十四份 guest raw、二十四次 focus success；此前六例、103 项
报告和所有 `.2`/probe 原件不改写。这仅增加 Server 上的恢复样本，不授予 Win7
OS32、其它系统恢复、快速快捷键前台拒绝、长期驻留或完整 #27/#29/生产资格。

## preview.3 追加快速输入诊断（2026-11-03）

在原 Win10 VM 复用完全相同的 `.3` 消费者、SDK、BuildID 和冻结源码
`d5a7ad96…`，另用三枚唯一 token 各启动一次，未编译或替换载荷。新只读 ISO
7,856,128 bytes，SHA-256
`631f7302de3be49d48fe6cc5f754928c891772e67458d524767b64c3df8a0970`；
两命名空间各 114 文件/2 目录完成闭集及全字节回读。

每例先观察真实 SDK 菜单、按 Esc、至少 2,200 ms 不重定位，再按 Shift+F10。
两例 rapid 尝试点击更新后，紧接唯一一次 Ctrl+Alt+F9，中间没有截图或等待。
host 点击开始至按键开始分别 20/14 ms；该 host 顺序不认证 guest 已提交选择。
实际第二菜单均返回 selection0/hasSelection=false，无 tooltip update 事件，随后
`return_focus()` 准备被拒绝、明确没有尝试 NIM_SETFOCUS，先请求退出 1 才收到
快捷键通知。没有第三次菜单调用，不能把这两例称为旧 `E_MENU_FOREGROUND`
复现，也不能计算不存在的 update→hotkey 间隔。

| case / token | 第二菜单 → 焦点结果 → 退出请求（native ms） | native / Owned |
|---|---|---|
| x64 rapid / `467e2eedf1c74fd5af208ac5591cfda1` | selection0 @39,873 → preparation rejected @39,960 → exit1 @39,964 | native1 @40,503；root-failed1 @41,080 |
| x64 delayed / `e29584a0094a41de83cb9a63c9218b6b` | selection1 @108,569 → focus success @108,820 → tooltip1 @108,827 | native0 @150,108；completed0 @150,459 |
| x86 rapid / `15ab56fadd6541179aff8e16cb55ea22` | selection0 @30,164 → preparation rejected @30,300 → exit1 @30,305 | native1 @30,781；root-failed1 @31,299 |

对照例在更新后取得新界面状态，再显式等待 10,011 ms，唯一快捷键触发真实第三
菜单并明确选择退出。完整原生 tooltip→hotkey 通知间隔为 32,344 ms；不能把
host 显式等待等同于完整间隔。对照图标位于隐藏区，而两例 rapid 位于主任务栏，
且对照前曾误开通用任务栏和网络菜单，均已排除。这不是严格单变量因果比较。

本轮首次有真实 FIFO 覆盖：对照 ordinal8 的原 message/wParam/lParam 入队，
187 ms 后同 tuple 实际回放，SDK 响应成功。它只证明该次回放；既有策略允许
depth0 的 direct 通知越过待处理项目，不承诺全局 ordinal 有序。x86 rapid 的
ordinal16 入队后随退出撤销丢弃 1 条；退出后 ordinal17 被取消、未调用 SDK。
取消 receipt 不能算作实际 SDK 分派成功。其余既有失败均原样保留。

三例均 cleanupConfirmed、自然闭合、Job0，无超时或强清；原预算保持。
7 次焦点响应中 5 次 success、2 次 preparation rejected，后者零 NIM。原始
日志仍不单列 NIM BOOL，不从成功响应宣称额外的直接 BOOL 采样。
只读精确导出 15 份 guest 原件至 `out/tray-focus-20261102/raw-sdk3-rapid-menu`，
bytes/SHA/readback 全部匹配、句柄已关闭、磁盘 metadata 不变；原 VM 正常 ACPI
退出 0 后已释放，free/stopped/globalQEMU0。

export manifest 为 8,806 bytes，SHA-256
`c096b72b901431dd2cfa8b4c5ef5bc5483539df13c0e5526883f1a61be5eaecc`；
运行汇总为 167,632 bytes，SHA-256
`54479a915515e2720d922e68192c9900dc44297f47a226657111199e210fed9b`。
独立复核完成 4,087 项完整性检查，仍判一例成功、两例失败；报告 SHA-256 为
`b14e8b3947c9784bc86ba9ac93614f5719e84db1031c25c8ac00868154ea5cb0`。
本轮三例/15 份与之前八例/34 份分开计数；旧 `.2` 前台拒绝和本轮两个 `.3`
准备拒绝仍开放。下一步只诊断具体拒绝条件，不把该失败放宽成通过；Win7 OS32、
完整 #27/#29、长期及生产门槛仍未完成。

## 外部 POD 诊断：激活成功后的 NULL 前台（2026-11-03）

为区分统一准备错误的具体判断点，另建仅诊断的外部程序，复制冻结 `.3` 的消费者
及 tray/menu 实现，在原观察点写固定 8,192 项 POD；保留原查询短路和 NDJSON，
只在原 run.finished/cleanup 后输出新增 trace。它不是普通安装 SDK 消费者资格。
四源经独立审查，无新增时钟/Win32 查询、GLE、激活、消息泵、post、等待或重试。
首次构建在编译前被 workspace/out 路径门拒绝；仅修外部包装路径后的全新构建
成功，两个 MSVC 阶段自然 0，Owned0/Job0，无强清。旧门禁失败保持。

成功构建原件为 21,453 bytes，SHA-256
`f4f6c4039e680879dc0473d1679e17342614a2f7729f3c99dfc8c0b99e562345`；
诊断 EXE 169,472 bytes，SHA-256
`5f37d65179e58b495045231bf42e5b35c348a18de59785a337a4b385d69de543`。
完整 MAP 将 tray/menu 归属到显式诊断对象，cursor scope 来自安装 SDK archive；
source closure 和无 FORCE 链接另核。中文 VERBOSE:LIB 只有库搜索行，没有
per-member Loaded 行，空匹配不当作完整库加载证明。

只读 ISO 为 4,091,904 bytes，SHA-256
`41980da9819855cafc657b637081f4fce2c34427815a6fc6022f1edebf54a464`；
58 文件/1 目录在两个 namespace 均完成闭集和全字节回读。原 Win10 上两枚新
LNK/token 各启动一次、各一次主要快捷键，先真实 Esc→至少 2,200 ms 原位等待→
Shift+F10，再尝试更新后以立即/固定 250 ms 间隔按 Ctrl+Alt+F9。两例主/隐藏通知
区不同，不是严格单变量比较。没有第三菜单或明确退出选择；原件分别为：

| case / token | 第二菜单 | 失败 call4 的首判断点 | native / Owned |
|---|---|---|---|
| immediate / `5884e5ae953c4ffea4fe98301af20a26` | selection0 @40,100 ms | SFW raw1、barrier raw1 后 `check.foreground_target`；known environment 的 foreground 为 NULL | native1 @40,586 ms；root-failed1 @41,391 ms |
| fixed250 / `a4e1f5d64483468eb2de998711c6e91c` | selection1 @89,301 ms | SFW raw1 后 `check.foreground_owner_or_target`；foreground 为 NULL，尚无 barrier | native1 @89,670 ms；root-failed1 @90,068 ms |

两次首判断点之前的 owner/目标身份、alive、session、input desktop、owner menu
检查均通过，最后预算仍余 897/949 ms，不是 FALSE 激活或超时。失败 call4 均未
调用 NIM；之前 call2 则直接记录 NIM raw1，原错误未查询，不填为已知 0。
固定 250 ms 例虽提交 selection1，焦点先失败而未执行 tooltip，也没有处理到
WM_HOTKEY；不能计算 update→hotkey 间隔。立即例的 WM_HOTKEY 在退出请求和
call4 返回后才被处理，不能归因为嵌套热键打断已执行的准备。

视觉消失不证明进程已退出。原 capture finished FILETIME 与下一 started 的精确
记录证明前一已闭合、后例随后启动，间隔 26.5763663 秒，PID/载荷各自匹配，
没有进程重叠。两例 cleanupConfirmed、自然失败、Job0，无超时或强清。trace
99/105 项均完整、dropped0；typed query=known 的 NULL 观测是真实记录，但其
内部原因仍未知。[Microsoft 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getforegroundwindow)
说明失去激活期间可能返回 NULL，只支持等待的合理性，不证明此处必会恢复。

两例十份原件完整只读导出至 `out/tray-focus-20261102/raw-pod-focus-diagnostic`，
manifest 6,146 bytes，SHA-256
`72c9386fc7e40d3274caa7e712172d22885790e183fea24a7539617b00242f88`；
汇总 117,274 bytes，SHA-256
`d979d81b525e1a7f30772d342c64e7baba744e1a28f9246a9b1944510864c83f`。
VM 正常 ACPI0、只读句柄关闭、磁盘 metadata 不变，已释放为 free/stopped/
globalQEMU0。诊断两例/十份与 `.3` 正式消费者各轮分开计数，旧前台激活失败、
本次两个准备拒绝及所有生产门槛保持。后续 `.4` 的安全 NULL pending 窄修先更新
ADR，再执行回归和真实新载荷验证；本节不是修复通过声明。

## preview.4 实现与首轮无桌面回归

按上述 ADR 在生产 `tray_service.cpp` 引入 unsafe/pending/accepted 的前台准入
结果，readiness 另区分 foreground pending 与普通 GUI 未就绪。成功 SFW 后、
全部元数据仍安全的 NULL 只可等待，不能授权 NIM；最终复核必须取得新的完整
ready pair。只有最终 foreground pending 回到原 waiter，原 GUI 焦点失配仍
立即拒绝。原绝对 1,000 ms、共享 256 次、单次 SFW/barrier/NIM、FALSE 和取消/
重建/所有权规则保持。两次独立源码审查无阻塞，生产实现 SHA-256
`4770d4a8bf90971b37f40f87d21f88c3c2173173853f53554cb7d6a0f6626d26`。

原 41 组函数、断言及运行顺序保留，新增 8 组覆盖激活/barrier/最终前后 NULL
恢复、持续 NULL/共享次数/绝对预算、pending 中十二类安全变化、实际原生失败及
初始 NULL 兼容路径。冻结 SDK3 的外部 RED harness 实际运行 49 组：原 41 组
全部成功，新 8 组中 7 组失败、1 组兼容检查成功，native1/Owned root-failed1、
Job0，无强清；测试失败是真实旧行为，未改成通过。外部汇总对预期 root-failed
曾错误判为非自然，原记录保留，后补离线判读不重跑或覆盖。

GREEN 的 native configure/build 均返回 0，但编译器后台进程未自然退休，整轮
Owned root1、执行了自有 Job 清理，fake 尚未启动。这一构建继续失败。其它任务
随后出现活动编译，按条件清理孤立 VCTIP 的预检拒绝，未停止外部进程。之后只
将已链接 fake 的源码、MAP、项目、EXE 与输入重新绑定，独立单次运行无桌面
测试：49/49 成功、native0/Owned0、63 ms、Job0，无强清。该报告明确
`priorCompileStillFailed=true`、`acceptedBuild=false`，仅确认测试行为，不认证
完整 SDK 构建或安装消费。

行为报告 5,570 bytes，SHA-256
`281f72aa3f45c4acc2743e72c68ec76e8cc7a27e6fc2653c3c0e2ed24e57ccc3`；
实现实际汇总 10,881 bytes，SHA-256
`f3a5c4d8ac6bf34141b7586435752e498e76b6ef165b5096e7bfe54ca446240c`。
TDD 的历史 x64 Runner 与当前正式工具有不同映像身份，但已有独立来源、原始
清单及五项相同源码核对，未假定只是时间戳差异；正式 `.4` 构建/消费者继续使用
本任务原固定工具。两架构 `.3` 安装目录各 467 文件、库、报告和四种 fake 已按
完整字节归档，避免后续 `.4` 覆盖输出时丢失旧证据。归档 manifest SHA-256 为
`c9c880d25de3a4f27d1698a9dd53445aa9c6a283d93940ede7ad7b38201c3fb0`。

VERSION、SDK/Web/React/Vue 与锁文件已同步 `.4`，GUI bindings 只新增精确 `.4`
契约，保留 `.1/.2/.3` 安装与回退；相关 Node 测试实际 38/38 通过。依赖版本及
integrity/resolved 未变。本小节形成时尚未完成 `.4` 正式双架构构建、安装消费者或
真实桌面输入；后续实测见下节，不能从这次 fake 通过关闭 #27/#29 或生产资格。

## preview.4 正式构建与 Server 实测（2026-11-03）

实际构建源码提交为 `814b641ac211b290f7ee152d51d0592ba852cedb`，冻结源码为
`cf822d00e0a736a1d68b80d6e37491c14b047f114d508c323714e5e87fb9a0d5`，
2,383 文件 / 178,229,047 normalized bytes。两架构使用原 MSVC v143
`14.44.35207`、Windows SDK `10.0.19041.0`、Release / MD。

| 实际检查 | x64 | x86 |
|---|---|---|
| 标准 Build ID | `ff7f1d81-56cd-4265-afe0-0c53cebc312e` | `945da6f5-477c-4f33-baf3-0773ffe4f799` |
| 正式构建、安装 | accepted；467 个安装文件、671 个 Workbench 文件 | accepted；467 个安装文件、671 个 Workbench 文件 |
| 精确 safe CTest | 14 项、84 组实际通过 | 14 项、84 组实际通过 |
| 仓库外消费者与 capture | 四份夹具源码，仅公开安装 SDK；实际构建通过 | 同左，独立架构绑定和 capture 五项常量均核符 |

84 组由 tray49 / menu15 / shortcut9 / resident11 构成。当前标准与消费者的
四个 Owned 构建过程及八个 MSVC configure/build 阶段均自然退出 0、Job0。
任务期间另按用户授权精确清理了 18 个外部来源未知的孤立编译后台进程，全部
如实记为强制清理；标准汇总保留当时 13 项，消费者汇总保留后续 18 项闭集及
两次部分执行后被活动编译拒绝的记录，不能把整个任务描述为“从未强清”。

首个正式 x64 尝试在完成四个 Web ci/build 后，被既存 VCTIP 门禁拒绝，仍为
失败。后续精确核对原始失败、四个 dist 的 433 个文件与 distribution 身份后，
通过官方 Web 证据入口续接当前新鲜 Web 产物，另用新 Build ID 执行原生构建和
安装；没有覆盖失败或重复 npm 构建。Web client31、SDK package smoke15、
版本/GUI bindings38 项实际通过；SDK Web、React、Vue 和 Workbench Web 四个
受检包的 npm audit 均为 0 项，不扩大为全仓库依赖无风险声明。

标准与消费者汇总分别位于 `out/tray-focus-20261102/` 下的
`workbench-build-preview4.actual.json`（60,794 bytes，SHA-256
`6cd345f8a7e3f1ebcc40d71d3b2a58fcf7810064da6797607015c10c4e900639`）及
`consumer-build-preview4.actual.json`（49,966 bytes，SHA-256
`f84287cf18de21ebab0feaf533b9bcbe44485064434174cb2fdc4a0c461db522`）。
首轮失败和 SDK3 归档由实际汇总保留，独立审查另存外部证据目录。

新只读 ISO 为 116,416,512 bytes，SHA-256
`253be885a12458657bb23b2d5a170d3148e0b9895dbaa81c8cbf14b5da3b77f4`。
1,465 个文件、八个新原生 LNK 均绑定上述载荷；两个 ISO namespace 都完成闭集
及全字节回读。预先计划八例并不代表八例已经执行。

Server 原 VM 新会话 `e1158f0c-045b-45c3-815d-8281161b7672` 串行执行前四例：
两消费者各通过 Task Manager 真实重启 Explorer 一次，恢复原目标图标，再完成
真实应用菜单 Esc、2.2 秒无重定位、ShiftF10、提示更新、唯一 CtrlAltF9 和显式退出。
两 Workbench 实际 X 关闭 slot3，再以 command3 恢复至 generation2 ready，
完成应用菜单 Esc / 原位 ShiftF10，以 command11 有序退出。

四例 native / Owned 均为 0，12 条 `tray.focus.ok=true`，Job0，无超时或强清。
消费者菜单序列均为 `[0,1,2]`、各有一次 `taskbar.created`；其
`sourceVerified=false` 保留，恢复结论结合真实 GUI。Workbench 菜单均为
`[3,0,11]`，每例原有 41 条 cursor pending、3 条 Runtime pending 仍保留，最终
Runtime、清理和有序关闭均完成；退出清理耗时 828 / 797 ms。记录建立 10,000 ms
预算的 `exit.cleanup-deadline` 不等于期限已耗尽。此四例 callback 全为直接分派，
不新增真实 FIFO 回放覆盖。`sdk4-server-x64-recovered-consumer-menu.jpg` 实际仍是
系统图标场景，另一次 x86 Workbench 通用任务栏菜单也被排除；不以文件名充当
应用菜单证据。slot3 / generation2 由原件和实际窗口重新出现共同核对。

14 份 Server 原件完整导出至 `out/tray-focus-20261102/raw-sdk4-server`，manifest
8,653 bytes，SHA-256
`f6138f3c839e669a16c1ec153130cade5e3094bdfc6f7c4559fb9e3da2cc9c66`。
前两例 capture FILETIME 与后一启动严格有序；WB64 的 `process.finished` 到
WB86 resident context 初始化，通过同一 boot 的 GetTickCount64 轴确认间隔
16,157 ms。没有额外记录绝对 Owned 退出 FILETIME，不冒充已知精确退出时刻。

## preview.4 Win10 立即输入仍失败，250 毫秒计划未完成

同一 ISO 在原 r12 新会话 `2086049e-3413-4608-8b30-0c03b915939d` 只启动了
两个 x64 新 token；两个 x86 用例暂未执行。立即输入 token
`2141959dd70245e897f3a706ea71d7bb` 的真实 Esc / 2.2 秒 / ShiftF10 前序成功，
随后同一 operator batch 尝试点击更新并立即发送唯一 CtrlAltF9，两个动作之间
没有截图或额外输入。原菜单实际 selection 为 `[0,0]`，没有提交更新或 tooltip；
第二菜单在 146,564 ms 返回，146,709 ms 的第二次焦点调用出现 `E_TRAY_FOCUS`，
明确为准备拒绝、该次未调用 NIM；首次焦点调用已成功。146,710 ms 请求退出 1，
快捷键通知直到 146,714 ms 才被处理。
native 在 146,974 ms 退出 1，Owned 自然 root-failed1、Job0，无监督超时或强清。
本次原件仍未暴露具体拒绝谓词，不能从 `.3` 的 POD 原因推定 `.4` 同一分支，
也不能将其改写为 NIM BOOL FALSE 或 `E_MENU_FOREGROUND`。

本例真实 callback ordinal9 曾入队并以相同 tuple 回放成功，queue age234ms；
ordinal18 入队后随退出丢弃 1 项，之后 ordinal19 被取消且未交给 SDK。它增加
组件级回放观察，不能改变整例失败或关闭完整重入资格。

计划 fixed250 的 token `53add7321c5b438d9dea614b4833eaec` 实际安装成功，并在
154 ms 输出 `interactive.ready`。操作员没有完成目标图标/菜单输入，callback、
menu、shortcut、focus 计数均为 0；原 native 180 秒期限耗尽，在 180,251 ms
退出 3，Owned 自然 root-failed3、Job0，无 Owned 超时或强清。它不是启动失败，
也不是有效的 250 毫秒对照。原操作记录中“未观察到图标”仅是视觉观察，不能
推断目标未启动；未执行的输入和超时均保留，不以重复启动覆盖此例。

两 capture 之间的同来宾 FILETIME 间隔为 58.4366502 秒，没有进程重叠。
十份原件导出至 `out/tray-focus-20261102/raw-sdk4-win10-triage`，manifest
6,375 bytes，SHA-256
`80d00086ccfe1565373b2c7c7d380da2668120bd8b7fabcee83dd2f3a695d0d5`。
两个 stderr 空文件也保留。两 VM 均正常 ACPI0、free/stopped、全部只读句柄关闭，
最终 globalQEMU0，原磁盘链及 CPU / 网络配置、凭据未改；只读导出前后的
磁盘 metadata 不变，不表示测试期间来宾没有写入日志。

下一步先为当前 `.4` 准备独立第一拒绝诊断，不猜测或放宽失败守卫。旧 `.2/.3`
失败、本轮立即输入失败、无效对照及 Win10 x86 / Win7 OS32 / 完整 #27/#29 和
生产门槛全部保持，`releaseReady=false`。
