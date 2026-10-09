# ADR-0046：显式启用的 Windows 本地 Agent 端点

日期：2026-10-09，验证记录更新于 2026-10-10。状态：已集成源码，完成下述有限双架构实际回归；不代表完整 SDK 发布验收。关联 ADR-0036、0037、0044；本记录先于实现写入。

端点复用现有 Windows 主体核验、permission_store 的实际连接与精确 scope，以及 bridge_transport 的本地动作和发现入口。构造不启动任何服务；应用显式 start，授权回调默认拒绝。既有 registry 必须先冻结，permission_store、transport、可选同一个 task_service 与原生应用上下文必须存活至端点真实停止。没有另一套授权或任务存储。

单个专用 I/O 线程管理一个 server named-pipe 实例和一个 outstanding 请求。名字只允许一个有界 ASCII 本地组件，前缀固定为 `\\.\pipe\`。创建使用 FIRST_PIPE_INSTANCE、maxInstances=1、PIPE_REJECT_REMOTE_CLIENTS 和 protected DACL。DACL 只允许当前原生进程实际 logon SID 的具体读数据、写数据、SYNCHRONIZE 和 FILE_READ_ATTRIBUTES 权限（mask 0x00100083）；缺失登录 SID或安全描述符时拒绝启动。客户端继续请求具体数据读写和 SYNCHRONIZE 权限，不能请求含 FILE_CREATE_PIPE_INSTANCE 的 GENERIC_WRITE。[微软安全文档](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)说明这两个权限位的别名关系。第一实例抢占失败即拒绝，不连接或信任已有同名服务。

DACL 0x00100083 adds only FILE_READ_ATTRIBUTES (0x80) to the earlier 0x00100003 mask. The unchanged diagnostic client still requests 0x00100003. On the current Windows development host, actual x64 and Win32 each completed 20 direct cases and the same 20 serial CTest cases with natural root0/supervisor0, including connection opening and generic-write/first-instance denial. No FILE_CREATE_PIPE_INSTANCE (0x4), GENERIC_WRITE, READ_CONTROL or other rights are added. This finite result does not establish a general Windows-version access-check cause or qualify other OS/accounts/sessions.

wire 为四字节 little-endian 非零长度及最多 64KiB 的严格 UTF-8 JSON。握手仅包含 kind=hello 和 schemaVersion=soui.local/1；必须完整实际读取后，专用 self-context I/O 线程才执行 verify_windows_named_pipe_peer。后续每个完整 frame 收到后、owner 派发前，以及每次发送提交前，再次核验并比较原始 PID、creation FILETIME、User SID、Auth LUID 和 session，并查询所持进程仍 alive。读取提交本身只检查连接关闭/停止 admission；owner 也复查 held-process alive。此核验保持 ADR-0044 的快照和句柄转交限制，不声称消除未来 token 变化或所有跨线程竞态。

请求沿用 soui.bridge/1 的 kind、schemaVersion、requestId、method、params、operationId 和 timeoutMs 子集。未知、重复顶层字段、凭据/主体/gesture/grant 字段、非法编码、深度或长度均拒绝。params 是有界对象，再由现有 typed action 契约验证。wire 不选择应用、窗口、文档、代次、session、origin、bearer 或能力名单；响应不包含 bearer 或原生主体。敏感参数和凭据不进入诊断。

start 记录真实 UI owner thread。authorizer、scope_current、业务与授权 UI 回调只在该线程的 pump_owner 调用，且不持端点锁。authorizer 接收真实核验对象，返回固定原生 scope 类型的 application/window/document/generation、原生 session/origin 与明确 capability/method 白名单。scope_current 必须确认真实应用/窗口/文档仍属于该代次。每个连接新建不可恢复的 native_connection_binding；issue_local 返回的精确 scope，即使发行与关闭竞争已 inactive，也先保存在连接控制槽，必须清理。

owner 队列只有单请求槽和不可被业务占用的清理控制标记。pump 同时有 count 和 wall-time 预算；预算不能抢占已经开始的应用 callback。执行前复查 held-process alive、连接 admission、scope_current 与实际 scope，再使用 handle_local；bridge.describe 直接使用 describe_local 的同一过滤。返回前再次核验 owner scope；发送提交前 I/O 在与撤销相同的连接锁中复查原始 peer、binding、scope 和连接状态。应用导航或关闭必须在 owner 调用 revoke_scope(native_scope_key)。

沿用 permission_store 实际默认 30 分钟期限；本切片不续期、不延长 lease。请求抵达 owner 时若精确 scope 已 inactive，而原生 scope、binding 与 peer 仍可核验，只允许固定 E_UNAUTHORIZED 控制响应；不能发送业务结果或发现列表。写入完成不代表客户端消费，I/O 在配置的有限读取预算内等待断开或一个弃置字节，让顺序客户端读取拒绝，然后关闭并清理，不解析或执行后续帧，不使用可能无界等待的 FlushFileBuffers。拒绝响应是有限 best-effort，不保证客户端实际读取。其他撤销或无法核验状态直接关闭。需要重新连接并获得新的应用授权，普通请求不自动重放；未来显式续期策略需另行复用 renew_local 的同 scope 与私有 bearer 轮换。

I/O 断开可立即 binding.close，禁止任何后续 resolve_local；精确 permission_store::revoke_scope 和 task_service::shutdown_scope 仅在 owner 控制槽执行。清理顺序是 revoke 精确 scope、请求同一 task_service 清理、关闭 binding、完成控制槽。shutdown_scope 只请求取消/清理，不证明 worker 已退出或业务回滚。重连须等待前一控制槽完成，然后生成新 binding；旧凭据、请求和代次不能复活。普通动作不会自动 retry，timeout 不等同于已开始动作的取消；断开或撤销后的结果丢弃，不据此判断动作未执行。

request_stop 禁止新 admission，关闭 binding，设置停止事件并 CancelIoEx。owner 在连接锁内选择 work 时同时读取 stop_requested，该选择点是授权/业务 callback 的 admission；已选择的 work 不承诺被 stop 抢占，尚未选择的 work 不再开始，精确清理仍有优先权。poll_stop 只在 owner 执行，先处理精确清理，观察实际 native thread 已结束才 join 并释放回调、pipe、事件与上下文。CancelIoEx 只请求取消；缓冲和 OVERLAPPED 在实际 completion 前不能释放或复用。[微软取消文档](https://learn.microsoft.com/en-us/windows/win32/fileio/cancelioex-func)明确这一点。取消尚未完成时保持停止 pending 与全部所有者，不在 UI 无界等待。析构要求真实停止已完成；违反对象寿命契约 fail-stop，不能 detach 或释放仍被原生线程借用的数据。

撤销与发送 admission 串行，但已提交的写入可能已经送出字节；应用 callback 的执行准入也不能撤回已经开始的业务。scope_current 回调和导航撤销是可信应用责任，必须与 owner 上的业务串行；检查与后续 OS 状态变化之间仍存在快照竞态。主循环必须持续 pump 控制槽，否则断开清理与重连会保持 pending。对象不得在自己的回调内析构，依赖对象不得与端点使用并发析构。

保留 C++17、MSVC v143、Windows SDK 10.0.19041.0 与 Win7 API 范围，不使用 GetOverlappedResultEx 或高版本线程 API。候选测试计划使用实际 pipe/握手、明确 DACL、抢占拒绝、native authorizer/owner 调用、有限 frame/队列、导航撤销、重连和停止边界。当前仅有同一 Windows 开发宿主上的上述有限 x64/Win32 结果；configure/build 的 root0/supervisor124 编译尾部 genericPassed=false 原始记录保留，不追认为通用成功。完整授权 UI、UIA、跨账户/登录/session、Win7 真机、长期运行、所有 Windows API 故障与完整 SDK 发布资格仍待验证；releaseReady=false。

## 公共 json_bridge 的原生端点组合

当前 json_bridge 是 final，registry、transport、permission_store 与 task_service 为私有成员。仅调用 allow_local_action/register_local_task_family 并不能从 json_bridge 用户侧构造使用这些实例的真实 Windows 端点。原有独立 runtime 端点回归没有关闭这个公开组合缺口。

增加 create_local_endpoint()：原生应用在全部方法、能力与任务注册完成后、首次 page bootstrap 前调用。成功时 json_bridge 持有唯一 endpoint，返回借用指针；对象仍为 stopped，不启动线程、不发行授权。分配成功后才冻结注册，分配异常保留原有注册状态；冻结之后的注册/能力设置及首次 bootstrap 后的创建均拒绝。调用者不删除返回指针，也不能由页面调用该工厂。

工厂复用同一 transport、permission_store 和 task_service，不公开这些存储的访问器，不建立另一套任务或授权。authorizer/scope_current 仍由可信原生应用提供，默认 deny；业务与授权回调仍仅由明确 owner pump 执行，页面输入不能创建 native scope/grant。allow_local_action 不改变原有页面能力政策；本地 task family 仍不进入页面 grant。

方法/action、task handler/family、desktop/renewal 服务注册和 set_session_capabilities 均在现有 json_bridge mutex 下检查同一冻结状态。set_event_sink 与 set_owner_window 是 Host 的运行时借用绑定，保留既有行为以便工厂后正常 mount；它们不建立新方法或权限政策，也不提供页面授权入口。应用仍负责这些借用对象的寿命及既有线程同步。

json_bridge 的 endpoint 成员最后声明，析构在实现文件定义，使 endpoint 在任务服务、transport 和权限存储之前析构。应用必须先 request_stop 并在 owner 上 poll_stop 至真实 stopped，再销毁 bridge；pending 时继续保留所有借用者与上下文。端点 stopped 仅完成其 I/O 与控制清理，不是业务任务退出屏障；已注册任务 handler 的借用上下文仍必须存活到 bridge 中 task_service 的实际析构/join 完成，不能先于 bridge 释放。析构 live endpoint 沿用现有 fail-stop，不 detach、不隐式 UI 无界等待。Host 的页面关闭或 shutdown_session 不是 native endpoint 停止：应用必须另外以原生 scope 入口撤销并完成端点停止。

未启用工厂的源码调用顺序保持既有行为。此修改改变公开 concrete C++ 类型的对象布局及析构符号；消费者必须与本次 SDK 头文件/库重新构建并通过既有源码、安装和载荷身份门槛，不宣称与旧静态库的二进制布局兼容。安装/export 不增加组件或第三方依赖。

2026-10-10 的正常 SDK Native V10 验证在同一 Windows 开发宿主上分别执行 x64 与 Win32。每个架构的完整原生测试记录包含 20 个 endpoint 用例及 6 个公共工厂组合用例，均自然退出且没有失败或跳过；组合用例覆盖 stopped 创建、注册冻结、实际分配失败、bootstrap 后拒绝、真实 named-pipe typed action、同一已注册 task family 的结果及断开取消、页面与本地方法隔离。无 private 权限访问、TestKit、假时钟或备用 task_service。两个架构还各自安装并迁移 SDK，通过只链接 Soui::Sdk 的 find_package 独立消费者。消费者仅验证工厂符号、布局、stopped 生命周期和策略冻结，不建立 live pipe。

记录位置为 `out/native-v10/x64/summary.json`（SHA256 `1fd57d4c3ffef5ae452b9e7e9ae9d9ed63cd696a3e4aa21d8b03818676336fe2`）和 `out/native-v10/Win32/summary.json`（SHA256 `7c9296916abff2e2cadd5878b6890be3f2c047c115a0f9347ca757dee0449c6d`）。它们绑定当时的实际源码、编译与安装载荷；其完整测试数还包含独立 UIA 等当时工作树内容，不能视为本切片单独提交后的完整 SDK 重建结果。本切片的精确提交快照仍须重新核对构建与安装组成。历史独立端点 configure/build 的 root0/supervisor124 记录保持 genericPassed=false。Win7、跨账户、长期业务、完整 GUI 与发布资格均不提升，releaseReady=false。
