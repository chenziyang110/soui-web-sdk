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

原生独立控制组另有十六个 case、三十五份 raw：十二次已完成 `NIM_SETFOCUS` 仍全部
返回 FALSE；两组未调用超时、两组在结果前异常退出分别保留，未知 BOOL 不记为成功。
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
该客户端切片与既有 Server 样本均为 FALSE，不证明 early-reply 的实际运行分支、
真实键盘焦点资格或系统级根因；原生日志也未认证 Esc 手势。两份新增原件与导出
manifest 的散列已匹配，记录位于 `out/tray-focus-20261102/raw-win10-baseline`。

该仪器化样本把失败收窄到 Shell 接收返回值，不能据此确认具体 Explorer 内部函数、
所有系统版本的根因、焦点效果或零时序扰动。marker 的 RAX 不是 API BOOL，
诊断 LastError 不是原发送错误；正常保留的 0 字节拒绝文件也不是有效拒绝记录。
该独立诊断不修改公共 SDK 或系统代码，早期两次致命异常仍保留。

本次修复的是调用方业务流程，未改变公共 SDK、严格托盘消费者或 API 失败定义。
新的安装后严格消费者、Win7、x86 真实 Esc、完整 #27/#29、长期驻留及生产资格仍待验；
没有发布、完整 Collector 或 `releaseReady` 变更，也不声称焦点已实际归还。
