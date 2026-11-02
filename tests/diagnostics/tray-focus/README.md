# 托盘焦点原生诊断

这是 #27/#29 的独立 Win32 诊断，未链接 SDK，不是产品修复或功能验收。每个进程只选择 `--identity hwnd` 或 `--identity guid`；GUID 模式在 ADD、SETVERSION、SETFOCUS、DELETE 和 GetRect 中使用同一个新 GUID，`uID=19` 仅用于 v4 回调过滤。HWND 模式始终使用 owner HWND/id19。两种模式均检查 ADD 和 VERSION_4。

唯一 GUI 操作者在获授权的任务 VM 中直接由 Explorer 启动 EXE，或使用只指向本 EXE、携带已审阅参数的原生快捷方式。不得在宿主运行本程序，不在终端、Run 或 Explorer 地址栏输入命令，不模拟键鼠，不主动重启 Explorer。当前来宾若锁屏，应由用户手动解锁。外部既有 Owned 监督器负责真正进程身份、原生退出与 Job 闭合；本程序只自报自身观察，不自授 `Job0`。

必需参数为 `--identity hwnd|guid --output C:\fresh\guest-directory`，可选独立参数 `--observe-keyboard`，顺序可交换；输出目录必须不存在、父目录已存在、使用绝对本地盘符路径，并位于当前工作目录之外，禁止祖先重解析点。程序仅新建该目录及 `events.ndjson`，不覆盖旧证据、不写启动 CD。`--help` 打印帮助并显示原生帮助框；参数错误打印错误及原生提示框，返回64，不创建图标；输出目录无法创建时显示原生错误框并返回2，不能计作已运行诊断。不要在宿主执行帮助或错误模式代替源码审查。

从仓库根目录仅编译（每架构使用新的仓库外目录）：

```powershell
cmake -S tests/diagnostics/tray-focus -B ../tray-focus-build-x64 -G "Visual Studio 17 2022" -A "x64,version=10.0.19041.0" -T "v143,host=x64,version=14.44.35207" -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build ../tray-focus-build-x64 --config Release
```

CMake 3.24–3.26 使用 `-A x64` 并保留 `CMAKE_SYSTEM_VERSION`；x86 用独立目录及 `Win32`。目标使用 C++17、静态 CRT，不加入根默认构建或 CTest。编译成功不证明参数、Shell 或真实交互通过。

操作员只做一次图标右键，在实际菜单可见后按一次 Escape。原生 `TrackPopupMenuEx` 返回0且错误0不能独立证明 Escape，日志保持 `escapeObservedByProbe=false`，实际输入与菜单可见/关闭须有外部画面证据。菜单返回后投递 WM_NULL 和完成消息；在真实 GetMessage 循环取出两消息、原回调深度0、`InSendMessageEx=0` 后才允许一次 NIM_SETFOCUS。没有额外激活、焦点重试或自动重启。

默认 `api-single-call` 模式完成五次观察后立即收尾。新 `--observe-keyboard` 模式另记为 `keyboard-route-observation`：完成五次观察后保留图标，在原60000ms期限内等待同一 v4 图标 ID 的第二次 `WM_CONTEXTMENU`。操作员在第一菜单 Escape 后不得用 Win+B、Tab、鼠标移动或点击重新定位焦点；直接真实按一次 Shift+F10。第二回调仅记为 `keyboard.candidate`，`sourceVerified=false`，程序不能独立认证输入来自键盘。该回调之后才显示标题为 `SECOND MENU: keyboard-route candidate` 的可辨菜单，操作员保留实际可见截图，再真实按 Escape。为使第二菜单正常接收 Escape，只有第二回调已记录后，程序才可在 `keyboard-second-menu` 阶段至多调用一次 `SetForegroundWindow(owner)`；其日志明确排除于第一次焦点证据。等待期和五次焦点观察期间不会额外激活，不调用第二次 NIM_SETFOCUS。第二菜单的显示位置仅取当前鼠标坐标，不证明回调坐标有效。

第二动作缺失、阶段错误、重复回调、选择菜单项或到原期限均返回2；第二菜单无选择关闭且其余收尾完整时，原 NIM_SETFOCUS FALSE 仍返回1，不能被键盘候选或实际画面升级为0。第二菜单无选择也不能自证 Escape。实际菜单、物理 Shift+F10/Escape 及未重新定位焦点的连续链须由外部 GUI 证据判定。这是新的观察模式，不能与旧模式或已冻结 raw 原件冒作同一实验；旧源码和构建原件应保持不变。

从 main 开始共用60000ms原始单调期限。焦点调用返回后的0/16/50/200/1000ms仅取样，记录实际取样偏移和迟到量；消息调度和查询耗时可能使样本迟到，绝不改写为精确按时。到原期限后不接纳新观察，进行已持有资源的有界外部监督下清理；缺样本、超期、非法阶段、额外右键或清理失败返回2。SETFOCUS FALSE保持返回1，即使实际前台改变；TRUE且完整收尾返回0仍只表示 API 确认，不代表已经证明通知区键盘焦点。原生 API 若停滞仍由外部原期限监督，不能靠刷新期限获取成功。

日志记录身份模式、实际 NID 字段、原始 BOOL、诊断用 LastError、GetRect 的 HRESULT，以及 owner、Shell_TrayWnd、foreground、实际 focus HWND 的类名、parent/root/rootOwner、PID/creation FILETIME/image、token elevation/IL/session/UIAccess、线程桌面和 input desktop、GUITHREADINFO、加载的 shell32 路径与文件版本。GetRect 仅 `S_OK` 记录位置，不把 `S_FALSE` 当成功。相关窗口最多记录8级 `GA_PARENT` 链，到首次查询的 `GA_ROOT` 为止，不扩展到无关的 desktop 窗口；每级只含精简 class、PID/TID/creation FILETIME 前后值、window/client RECT 与 client→screen 映射，不重复完整 token/image。`rootReached` 与所有节点身份均已知/未变分开记录，未知/变化/未到 root 的链保持不完整。当前 foreground TID 的 GUITHREADINFO 先取样，记录 active/focus 的 root、与该 foreground/root 的原始相等关系；整段前后另取 foreground 身份。后台 Shell 线程自身残留的 focus 不能替代当前 foreground 线程观察。

几何数据保留 API 原值：`GetWindowRect` 可能被 DPI 虚拟化，`MapWindowPoints` 给出调用线程坐标下的 screen 映射，GetRect 的屏幕坐标未由本程序统一 DPI。程序只读记录 `IsProcessDPIAware`，若系统提供则动态查询线程 DPI awareness，不改变进程或线程 awareness，不静态导入提高 Win7 要求。几何包含关系须在明确同坐标空间及外部 DPI/显示器证据后另行判断；日志保持 `iconRectCoordinateComparisonQualified=false`。类名与 Shell 父链不是公开跨版本稳定契约；未知/截断/缺失数据不能认证焦点。快照不是原子事务；首尾相同 PID/birth/HWND 也不能排除同一 Explorer 内 HWND 复用，不承诺持续焦点。

原始 DLL/EXE/源码散列由外部封存绑定，文件版本不冒充文件散列。查询失败明确记录 unknown/null 及原始错误；`GetLastError` 不解释为 Shell_NotifyIcon 的公开错误合同。GetThreadDesktop 为借用句柄，只有 OpenInputDesktop 的只读句柄由本程序关闭。没有同线程 UIA 查询。

事件最多128行、单行64KiB、总量1MiB。每条祖先链还限制节点 JSON 至6KiB；到界明确记录 `nodeByteLimitReached=true`/不完整，最终返回2，不用截断数据认证焦点，不放宽原行/总量硬限。日志写入失败使结果失败，原目录保留。输出包含本次测试进程路径、桌面与权限观察，应按测试证据处理；不采集页面、用户文件、窗口标题或凭据。

接口依据：[Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)、[NOTIFYICONDATAW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw)、[GetRect](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyicongetrect)、[GetGUIThreadInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo)。前台 HWND 改变或即时 focus 为0都不能代替通知区/UIA及实际键盘行为的独立判定。
