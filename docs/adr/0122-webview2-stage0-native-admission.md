# ADR 0122：WebView2 Stage0 原生授权与真实 adapter 退休屏障

状态：实验实现中；不冻结公共 DevTools API，不表示 Stage0 B 已通过。

## 背景

独立 COM 原型不等于 SDK adapter 集成。真实 adapter 已有独占 profile、STA 异步初始化、Bridge worker、非阻塞 unmount 和 ADR 0057 的 owning runtime exit watch。现有 reload 会复用 Core；worker 结束也没有单独报告 shutdown_session 的成功或失败。

## 决定

在真实 `webview2_host.cpp` 内增加专用编译宏保护的私有 Stage0 接缝，声明放在不安装的 renderer 私有头。公共头仅有统一的内部 friend 声明；普通构建不提供实验入口。默认策略关闭，prepare 前必须由原生应用同时接受销毁旧目标的终止策略并选择 exclusive profile。

原生 grant 绑定实际 adapter state、进程、文档 generation/document ID 和本地 grant epoch。仅 mounted、可信页面、实际 Bridge session、已取得且健康的 owning watch 才能授权。网页消息、普通 Bridge、命令行、环境变量均不提供授权入口。保持 `AreDevToolsEnabled(FALSE)` 和默认菜单关闭；受控入口统一检查后调用原生 `OpenDevToolsWindow`。

固定 SDK 的 loader 契约另允许环境变量/注册表替换显式 Runtime/profile 并追加浏览器参数。因此所有 WebView2 adapter 在创建 Environment 前拒绝这些外部 override；默认关闭也不能绕过此检查。七个环境名字为 `WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`、`WEBVIEW2_USER_DATA_FOLDER`、`WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`、`WEBVIEW2_CHANNEL_SEARCH_KIND`、`WEBVIEW2_RELEASE_CHANNELS`、`WEBVIEW2_WAIT_FOR_SCRIPT_DEBUGGER`、`WEBVIEW2_PIPE_FOR_SCRIPT_DEBUGGER`。只查询存在性，不读取正文；定义为空也拒绝，不临时清空进程环境来调用 loader。

注册表检查 HKLM/HKCU 的 `Software\Policies\Microsoft\Edge\WebView2` 下 `BrowserExecutableFolder`、`ChannelSearchKind`、`ReleaseChannels`、`AdditionalBrowserArguments`、`UserDataFolder`，覆盖适用 AUMID、进程 EXE 值名及 `*`（UserDataFolder 不使用 `*`），保守拒绝任一适用值，不读正文或改策略。查询失败拒绝。全部键缺失或无值不需要 AUMID；存在策略而无法完整确定进程身份时返回 `E_POLICY_ID_UNRESOLVED`。Win7 动态探测 packaged identity API，Shell32 explicit AppID getter 失败不猜作“无 ID”。这可能拒绝身份不可核实且存在其他应用策略的机器；此兼容限制明确保留，不能改为默许未知策略。应用可以通过其既有原生身份初始化提供可核实的 explicit AppID，不由 SDK 擅自设置或覆盖进程身份。

调用 open 前即把目标标记为可能暴露，失败也不撤销这一事实。成功仅报告 accepted；inspector visible/attached 仍为 unknown。该目标后续 reload/新文档导航被拒，撤销 grant 不清除此门禁。应用须在打开前处理未保存状态；不能用新 generation 宣称已有 inspector 失效。

强撤销先立即废止 grant、冻结 Bridge 请求/输出，停止接纳新业务；已进入的调用自然返回。原 worker 撤销其实际 session，独立发布 success/failure/unknown acknowledgement，再实际退出和 join。异常或无法确认 session 所有权不能成为成功证据。SDK 不回滚此前业务副作用，不在 UI 上同步等待 Bridge。

公共 Host close 继续驱动原 unmount。新增异步操作以及原有诊断 ExecuteScript 使用实际 completion 账目：提交前登记，真实回调先结算 pending 并持有 active borrow，再判断旧代际或 closing；同步提交失败也恰好结算一次。关闭中仍接收结算，不使用 live() 提前丢弃账目。Controller.Close 之后才等待可能受暂停影响的脚本 completion；active 回调期间不释放 Controller。事件注销、Close 和资源释放分阶段，重试不重复 Close。

敏感新目标只能在应用确认以下全部条件后创建：真实 scope 撤销成功、worker join、Host close 成功、全部新增异步 pending/active 为零，以及旧 owning watch 匹配 NORMAL 事件、原 held handle signaled、实际 finish 成功。exclusive 只阻止其他进程共享，同进程的 controller/profile 仍由应用唯一拥有。失败保持阻断；旧对象及 Bridge 保留至各自所有权排空。

私有验证仅提供固定 Debugger enable/debugger 语句操作及标量事件计数，不提供任意脚本或通用 CDP 网关，不记录协议正文、session/token 或业务内容。

## 备选与兼容性

不采用临时切换 AreDevToolsEnabled、猜测 inspector HWND、token 轮换或 Controller.Close HRESULT 作为真实退休证明。不新增退出监督器，不修改 host_config 或公共 Host 虚函数。普通 SDK 无可调用 DevTools 入口；现有 unmount 仍可能返回 E_BUSY，调用方继续在顶层 STA 消息循环重试。

## 验证

本变更只执行受影响的有限集合：原生默认/跨目标/grant 撤销门禁；真实 SDK 目标暂停时 Bridge 撤销与浏览器退休后新目标无授权；初始化取消/迟到 completion；一个新增外部调试 override 在 loader 前被拒绝的实际负例。编译和无浏览器测试只证明各自边界，不能代替真实 VM 交互。旧原型失败记录保持原结论，不改挂到新二进制。后续记录追加实际源码、库、EXE、Runtime 和运行证据。

## 依据

- Microsoft [DevTools 设置](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2settings?view=webview2-1.0.4258.31#get_aredevtoolsenabled)及[程序化调试入口](https://learn.microsoft.com/en-us/microsoft-edge/webview2/how-to/debug-devtools)：用户菜单/快捷键禁用不构成 native open 的授权机制；组合行为仍需固定 Runtime 实测。
- Microsoft [Controller.Close](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2controller?view=webview2-1.0.4258.31#close)及[BrowserProcessExited](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment5?view=webview2-1.0.4258.31#add_browserprocessexited)：目标关闭和 browser 实际终止分开确认。
- Microsoft [线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)：保持 UI STA 消息泵，不在回调内嵌套模态循环。
- Microsoft [loader override 契约](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl?view=webview2-1.0.4258.31#createcorewebview2environmentwithoptions)与[功能 flags](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/webview-features-flags)：外部参数和显式创建参数分开校验；PIPE 单独不是调试授权。
- Microsoft [packaged identity](https://learn.microsoft.com/en-us/windows/win32/api/appmodel/nf-appmodel-getcurrentapplicationusermodelid)和[explicit AppID getter](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-getcurrentprocessexplicitappusermodelid)：两个来源不能臆造优先级或把未定义 HRESULT 当作不存在。
