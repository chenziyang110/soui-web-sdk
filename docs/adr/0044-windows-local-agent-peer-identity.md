# ADR-0044：Windows 本地 Agent 的原生主体核验

日期：2026-10-09。状态：已应用，身份模块的双架构原生测试通过；SDK 接入和完整端点待验。关联 ADR-0036、ADR-0037；本记录先于实现写入。

## 背景

现有 native_connection_binding 由可信 C++ 控制层创建，证明 store 与连接对象的归属，但不验证 Windows 客户端。新增一个局部核验模块，从可信端点持有的已连接 server named-pipe handle 取得系统身份。模块不读取 wire 自报的 PID、SID 或权限，不创建 bearer、授权 scope 或 user gesture。

## 决定

verify_windows_named_pipe_peer 只接受一个借用的原生 server pipe handle，并复制为不可继承的临时句柄。调用方在整个核验期间持有原 handle，并串行管理该实例的读取、断开和重连；入口必须在专用 server 工作线程上执行。端点应以 PIPE_REJECT_REMOTE_CLIENTS 创建本地管道并自行设置 DACL，握手读取、消息边界、远程拒绝和管道创建均不由本叶片实现。

模块读取 GetNamedPipeClientProcessId 和 GetNamedPipeClientSessionId，随后以 PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE 打开并持有进程，取得 GetProcessTimes creation FILETIME。发布前再次核对管道 PID/session，并以 WaitForSingleObject(handle, 0) 检查所持进程仍存活。只读对象永久持有该进程句柄，后续存活查询不按 PID 重新打开进程。[Microsoft 的 PID API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid)、[session API](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientsessionid)、[进程访问权](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights)与[时间 API](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes)提供这些原生观测。

模块读取所持进程的 primary token，随后 ImpersonateNamedPipeClient / OpenThreadToken 读取实际管道 impersonation token。两者的 User SID、TOKEN_STATISTICS::AuthenticationId 和 TokenSessionId 必须一致，session 也必须等于管道的 client session。TokenId 不要求相同：AuthenticationId 才是登录会话 LUID。仅接受 TokenImpersonation 且等级为 SecurityImpersonation 或 SecurityDelegation；匿名 SID、SecurityAnonymous 和 SecurityIdentification 拒绝。SID 仅从有界 TOKEN_USER 返回缓冲区复制，不输出 token handle。[TOKEN_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-token_statistics)、[OpenThreadToken](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openthreadtoken)与[impersonation 等级](https://learn.microsoft.com/en-us/windows/win32/secauthz/impersonation-levels)支持此比较规则。

ImpersonateNamedPipeClient 使用该实例最后一次已读取消息的安全上下文。因此调用方必须先完成一条握手消息的实际读取，再调用核验；核验期间不得另一个线程读取或替换连接。仅连接成功不能满足此前置条件。本函数不把 API 成功误解为未读取握手的身份保证。[Microsoft impersonation 文档](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-impersonatenamedpipeclient)明确该来源。

句柄和 revert 使用 RAII。入口已有 impersonation 时返回 caller_already_impersonating，并清除该上下文，不恢复任意调用者 token；这个侧作用是专用 server 线程的明确契约。即使参数、分配或 API 失败，出口也执行 checked RevertToSelf。不能恢复 self 身份时立即 TerminateProcess，ExitProcess 为后备，不向仍 impersonated 的调用方返回。错误仅暴露稳定枚举和固定 ASCII 名称，不暴露 GetLastError、token 内容或异常原文。[RevertToSelf 文档](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-reverttoself)要求失败时终止进程。

windows_peer_identity 的构造函数私有，禁止复制和移动；只有实际核验工厂能创建对象。返回 unique_ptr<const windows_peer_identity>，字段不可变；只暴露 PID、creation FILETIME、SID 字节、Auth LUID、session 与 held_process_state。没有公有构造器或字符串恢复入口。它是核验时刻的快照，不能证明未来 token 未变化，也不能消除管道句柄转交、调用前 PID 复用或端点并发重连的所有风险；PID/creation pair 绑定的是实际打开并持有的进程对象，不能代替端点握手与连接代次管理。对象存在或 alive 均不代表授权。

## 备选方案

拒绝以 wire JSON 的 PID/SID 建立身份；拒绝只比对可复制的进程路径；拒绝把 User SID 相同当作同一个登录会话；拒绝以公开身份字段恢复连接 binding。也不接受只 impersonate 后直接授予能力的实现，身份验证与 authorizer 保持独立。

## 兼容性与迁移

全部 API 在 Windows 7 范围内；保留项目 C++17、MSVC v143、Windows SDK 10.0.19041.0 与 WINVER/_WIN32_WINNT 0x0601。实现使用现有 Kernel32 / Advapi32 依赖。模块已纳入 SDK 的源码构建输入，测试接入公共原生套件。实际 endpoint 的串行握手、authorizer、应用/窗口/文档 generation 和关闭顺序仍需接入；此切片不修改 permission_store 或 dispatcher。

## 验证

测试使用真实 CreateNamedPipe、CreateProcess 客户端、实际握手读取与 SECURITY_SQOS_PRESENT。覆盖实际 PID/FILETIME/SID/LUID/session，伪造 wire 身份无效，anonymous/Identification 拒绝，client-end/非法 handle 拒绝，已有 impersonation 的清除，所持进程由 alive 变 exited，以及普通失败路径和有界 C++ 分配失败的真实 handle/revert 清理。所有 I/O 与子进程等待有界；失败不伪造 cleanup 成功。2026-10-09，Windows 11 开发机的 x64、Win32 各完成一个程序中的 8 组真实管道测试，直接运行和 CTest 均正常退出并自然清空受控进程组；编译阶段保留控制台尾进程导致的监督器 124/generic false。不同账户、登录 LUID、跨 session、API 故障注入、RevertToSelf fail-stop 和 Windows 7 真机仍需要后续受控环境，不能用 wire 假值充当这些系统身份差异。本记录不宣称完整 endpoint、授权、应用窗口代次或 SDK releaseReady。
