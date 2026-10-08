# ADR-0027：本地主体授权范围与共享任务归属

日期：2026-10-08。状态：服务层首批切片已实现，x86/x64 无窗口回归通过，待独立审查。范围：05-07 / 05-08c 的公共授权和任务边界；下文管道主体和定向事件仍为设计，未实现或验收。

## 首批服务层实现决定

- `authorization_scope::scope_id()` 返回不可变 `soui-scope-<十进制序号>`。同一 SDK 模块装载实例使用一个进程内原子 uint64 分配器；授权、续租和撤销不重置计数，耗尽时 fail closed，不保存持续增长的历史集合。ID 可预测，只用于关联，不是 bearer、能力或所有权证明；不承诺跨进程、重启或独立静态 SDK 副本唯一性。
- `permission_store::revoke_scope(shared_ptr<const authorization_scope>)` 只撤销本 store 实际持有的 scope 对象，先失效再移除当前和重叠 bearer；空、其他 store 和已经移除的对象返回 false。字符串 ID 不被解析为授权。续租保留同一 scope 和 ID，已撤销对象不能复活。
- 任务 operation key 包含 `scope_id` 与 operationId；所有查询、取消、订阅和重复关联仍比较实际 scope 对象及 session/origin。相同 scope、kind/input 与 operationId 返回原 taskId，输入不同冲突；不同 grant 或不同 store 相同 operationId 独立。操作幂等仍只覆盖被任务保留上限保留的记录，不提供永久历史。
- `task_service::shutdown_scope(scope)` 要求先撤销或到期；活跃或空 scope 返回 false，避免错误顺序留下可继续提交的授权。通过后只移除该对象的订阅和请求协作取消，返回 true 只表示执行了服务清理请求，不表示处理器已退出。现有 shutdown_session 及页面 API 保留，应用仍先 revoke 再关闭服务。
- 排队工作在 worker 资源锁内、标记运行前重验实际授权与 task.start 能力，事件回调后、业务执行提交前再验一次；撤销或到期后的未执行工作不能进入业务回调。最后复验成功是执行提交边界，随后到期不强制中止已执行处理器；事件不再新派发，显式 shutdown_scope 请求协作取消。执行提交之后的业务副作用不承诺回滚。
- finish 不把 cancelRequested 无条件覆盖为 cancelled。已执行处理器返回 success/partial/failure 保留；只有未执行便被取消，或处理器明确返回 task_cancelled/user_cancelled，才形成 cancelled 终态。task_store::complete 原本按实际 outcome 映射，保持不变。旧合作处理器测试改为观察取消后明确返回取消结果，保留原终态断言。
- exact-scope 清理不影响同文档其他 grant；任务保留淘汰同步移除对应 operation 和订阅，回收容量。仍只有原有全局事件 sink：本层不增加外部 Agent 订阅或声称 scope-directed event 可用。原生入口建立本地主体后仍须禁止本地 subscribe，直到定向出口独立实现。

验证使用受控时钟和 condition_variable/barrier：跨 grant/store 幂等、续租与精确撤销、执行前排队撤销、真实取消结果、跨 scope 查询/订阅、一个范围关闭不误伤、容量与淘汰回收。x86/x64 无窗口验证绑定 SDK19041 和受管进程 Job；不运行 GUI、VM 或真实输入。

## 背景

当前 `authorization_scope` 已能在 bearer 续期期间保持 C++ 对象身份，任务拥有该对象而不是某一时刻的 token。但授权仍只绑定文档 session/origin；任务幂等键仅为 `session + operationId`，同一文档的不同原生授权范围会争用操作标识。事件出口也只有一个全局 sink。

本地 Agent 需要真实 Windows 连接身份和独立撤销。页面传来的 PID、SID、actor、权限或 token 不能成为可信主体。直接把页面 `SouiGui.perform()` 发布到管道，会绕过原生业务校验，无法满足发现、快照和执行按主体过滤的目标。

## 决定

继续使用一个 `permission_store`、公共动作 dispatcher 和 `task_service`。不增加另一套长任务实现。原生 UIA、网页与本地 Agent 通过各自入口进入同一业务注册和最终校验，但不共享身份声明或手势权限。

每次原生授权获得独立的稳定 scope 标识。该标识不等于 bearer，不授予权限；它在续期时不变，撤销后不能复活，也不能被另一 grant 复用。任务幂等键包含稳定 scope 与 operationId。相同 scope、相同 operationId 和输入关联原操作；不同输入拒绝；不同 scope 的相同 operationId 独立。任务、订阅和结果查询继续检查活跃的实际 scope 对象，不能只比较公开字符串。

增加只撤销指定 scope 的原生入口，与按 scope 的任务关闭入口。断开一个 Agent 连接不能取消其他主体或同文档页面的任务。撤销必须先使授权失效，再清理该范围排队工作、订阅及协作取消；排队任务提交前在资源锁内再次检查，已经提交的业务副作用不承诺回滚，取消请求不能提前变成已取消终态。

本地主体绑定包含：应用实例、窗口代次、文档会话、真实客户端进程 PID 和精确创建 FILETIME、Windows session、用户 SID、logon AuthenticationId、完整性等级，以及本次连接 ID。该数据只由原生 Windows 身份验证层产生或由可信原生应用配置，不能从请求 JSON 反序列化为权威数据。

本地 grant 默认不创建。应用原生配置显式启用端点，原生 authorizer 对经过验证的主体决定可发现的节点、字段、业务方法和能力。相同 SID 只允许进入认证流程，不等于授权。复制页面 bearer、其他连接 bearer 或伪造会话字段不能使用本地 grant。现有页面 resolve/lease/renew 入口拒绝本地绑定 grant；本地入口须携带本次连接的可信绑定才能解析和续租。客户端也核验真实服务端进程身份，端点名称不充当认证。

断连、客户端实际退出、原生撤销、窗口关闭或文档重载均失效相应 grant。重连创建新的连接和授权范围，重新授权；不会恢复旧调用的执行权。续租只改变内部凭据，保留任务归属。超时或丢失回复之后按原操作查询，不自动重放业务动作，真实手势受限动作不自动重试。

当前单个全局事件 sink 不能同时安全地服务页面和独立主体。首个本地传输切片采用显式 query 等待任务，不提供 subscribe；对 subscribe 返回明确的能力未提供错误。若随后增加订阅，须先实现原生按 scope 定向路由，并在最终发送时再次核验活跃连接、窗口代次和授权，不能把本地事件送到页面全局 sink。

发现、语义快照、输入、结果、任务进度、错误和日志均由原生字段及方法白名单限制；过滤先于分页。密码、密钥与未允许的页面状态不进入镜像或传输日志。TS 的 `sensitive` 标志不能扩大或代替原生策略。WebView2 自身已公开的辅助功能 DOM 不在这个镜像过滤的隐藏保障范围内。

本地 Agent 与普通 UIA 入口始终不具有 renderer 输入产生的手势权限。不得用 `SendInput`、`PostMessage`、DOM click 或自动临时授权伪造真人输入。GUI 语义动作需要映射到已注册的原生业务方法；任意页面 JS 处理器仍只属于页面内接口。

## 备选方案

以 rotating token 作为幂等键会让续租后重新执行。只有 session/origin 会把同文档不同主体混在一起。为 Agent 新建任务服务会复制取消、容量、保留和错误语义。把 JSON actor 或管道名称当成身份不能证明实际调用者。仅依赖 TS 过滤无法约束直接访问原生端点的客户端。

## 兼容与迁移

现有页面 issue/resolve 和任务 API 保留，新增稳定 scope 归属用于修复多 grant 操作冲突；这不扩大网页权限。作用域键只保证进程内、仍被保留的操作身份，不承诺重启后的持久幂等或跨重载恢复执行。

公共方法、错误和能力探测以实现后的头文件及迁移文档为准。低层接口不提高 Windows 7 SP1 或 C++17 最低要求。管道身份验证、DACL、分帧/容量/断连生命周期，以及有限 UIA 各有独立 ADR；本决策不会单独使它们可用。

## 验证

使用 barrier 和可控单调时钟验证同 operationId 跨 scope 隔离、续期不重复执行、跨主体查询拒绝、一个 scope 撤销不影响其他范围，以及排队后撤销的执行前检查。验证假的 JSON 主体、复制凭据、错窗口/文档代次、断连后迟到回复与重连旧请求均拒绝。

真实独立管道客户端须验证 Windows 实际 PID/FILETIME/token 身份和服务端身份；API 查询及 impersonation 失败路径分别执行。目标 x86/x64、Win7+ 与 PE 缺组件验证单独留证。模拟身份或状态测试不能当作真实目标系统或屏幕阅读器验收。
