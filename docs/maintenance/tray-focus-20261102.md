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
