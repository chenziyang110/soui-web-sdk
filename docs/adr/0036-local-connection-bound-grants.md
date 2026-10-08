# ADR-0036：本地连接绑定的原生授权

日期：2026-10-09。状态：已接受；本机两架构策略与所选原生回归通过独立复核，端点与系统身份验收待完成。关联 05-08c；以已完成的 scope/operation 查询为基础。

本地 Agent 的第一层是授权隔离。页面 bearer 即使被复制到另一条调用路径，也不能变成本地连接授权；本地 bearer 即使被页面获得，也不能被普通 Bridge 接受。公开 scopeId、进程路径或 SID 都不是授权凭据。

本切片只增加 permission_store 的原生连接绑定与授权类型，不开启管道、创建线程或宣称验证 Windows 客户端身份。后续端点必须在实际身份验证、原生 authorizer 和应用/窗口/文档生命周期检查之后，才能创建并授权此绑定。直接调用这些 C++ API 的应用属于可信原生控制层；网页没有构造或传递绑定对象的入口。

native_connection_binding 是不可复制的原生对象。由一个 permission_store 创建，绑定不可变的服务端 session/origin，并记录该 store 的实际私有对象身份。调用方保存 shared_ptr；重连必须新建对象，不能以字符串或公开编号重建。close 不可逆，立即使依赖它的 scope inactive；close 只表示禁止新授权，不是任务/回调或资源关闭 barrier。端点仍须先 revoke_scope，再 shutdown_scope，完成自身 I/O 和句柄清理。

issue_local 只接受本 store 创建且仍 active 的实际绑定，明确的 capability 与 method 白名单。返回 issued_local_grant {token, scope}，始终交付精确资源归属对象，不提供表示可执行的 ok/bool；scope::active 或 scope::allows 也不替代实际 method 的 resolve_local。若关闭与发行竞争，新 token 可能已不可使用，但端点仍可精确 revoke_scope/shutdown_scope。全部可能分配的返回对象在 grants_ 插入之前准备完毕，插入成功后仅无抛移动返回；页面 issue 保持 string 返回兼容。

每名单最多 64 项；method 为现有 1–128 ASCII 标识，capability 非空、最多 512 字节有效 UTF-8。非法、重复或空策略拒绝，不静默扩权。session/origin 沿现有 Bridge 的有界标识和 app/file 来源约定，不从连接 wire 复制。

页面与本地授权共用现有 scope、30 分钟有效期、提前 5 分钟续租、30 秒旧 bearer 重叠和 operationId 幂等规则。授权 state 保存 grant kind、实际绑定和本地 method 集；普通 resolve/check/lease/renew 只接受页面 grant。本地 resolve_local 从实际绑定取得 session/origin，同时核对 store、scope 的实际绑定、bearer、capability、method 和 active 状态；页面 grant 在本地入口始终拒绝。两类 bearer 均继续由系统熵产生，不把类型编码当凭据。

本地 lease/renew 是独立原生入口，需要同一个实际绑定和白名单中的 bridge.session.renew，以及对应 capability。续租保留同一 scope；更换 bearer 不改变连接或策略。绑定关闭、store 销毁、scope 撤销和过期不能被续租复活。页面及本地 grant 的所有 bearer alias 均纳入原有 revoke_scope/revoke_session，store 销毁使借用 scope 和绑定失效。

close 与已开始的发行/续租不是完成 barrier：close 可能抢在返回之前，发行会返回 inactive scope，续租可能报告成功旋转一个已经 inactive 的 token；后续授权仍拒绝。端点必须串行化连接 grant 登记和关闭，并在真正执行边界重新 resolve_local；返回结果不是关闭后的可用性承诺。state 强持有绑定，因此丢失调用者 shared_ptr 不自动 close；端点负责明确关闭。store 析构不得与成员调用并发，必须先排空使用者；本切片不添加这种对象寿命同步。

本地方法白名单不能仅按 capability 实现：两个方法共享一个 capability 时，只允许显式列出的那个。下层 authorization_scope::allows 继续表示 capability 的有效性；未来统一 local dispatcher 必须在实际执行边界调用 resolve_local，不得绕过 method 校验直接执行 registry handler。任务 kind、敏感结果投影、真实手势拒绝、发现分页过滤和 GUI generation 仍属于后续 dispatcher/端点切片；本层没有完成这些能力。

验证直接覆盖两个路径互拒、复制 bearer/foreign binding/store、同 capability 不同 method、关闭/撤销/销毁/过期、续租前后相同 scope 和旧 bearer 窗口、重连新对象拒绝旧任务归属，以及受限策略的拒绝。保留已有页面续租、任务 scope 和 operation 查询回归。受控时钟只证明策略，不代替真实 8 小时测试；原生无窗口回归不代替管道、GUI、Win7/PE 或最终 SDK 验收。不会写入 canonical acceptance 或宣称 releaseReady。

当前 Win11 的 Release x86/x64 完整静态 SDK 与六组所选原生目标分别完成真实编译、CTest 6/6及本地策略10/10，六个 owned Job均自然闭合。关闭后的 queued probe 在全部 worker进入后置FIFO sentinel、服务尚未析构时确认没有进入业务。证据与未复制的二进制边界见[持久归档](../../verification/production-validation/0.5-local-grants-2026-10-09/README.md)。
