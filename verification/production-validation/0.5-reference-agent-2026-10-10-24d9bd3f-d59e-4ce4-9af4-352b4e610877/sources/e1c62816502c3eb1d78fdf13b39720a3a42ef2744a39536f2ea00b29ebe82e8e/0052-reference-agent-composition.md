# ADR 0052：参考工作台的显式本地 Agent 组合

状态：out-only 首片实现候选，尚未执行。以完整 reference 源 V5、当前公开 json_bridge 工厂及 ADR0037/0039/0044/0046 为前提。原设计先于接入代码并已由 Root 审阅；SDK GUI 异步扩展仅为后续方案。

V2 边界修正：首次实际 binding 前的 Host 初始旧 session 清理仍调用真实 Bridge shutdown，但不关闭从未准入的 listener。owner 第一次可信文档 probe 成功后发布单调 atomic binding_started，发布后和实际 start 后都重新检查原生 receipt/digest。shutdown_session 在真实 Bridge 调用前、调用后及异常退出都观察该 latch：任何重叠/绑定后失效永久 close，不清 latch、不重新授权。真正完成于 publication 前的清理不授予权限；后续仍必须通过明确 CLI、held peer 和实际 owner 文档检查。selected-peer 例中有实际 foreign prebind 清理/旧 session 拒绝/新 bootstrap，以及发布后第二 probe 与真实 foreign shutdown 的确定性交错，证明此时没有 listener、不会重新开放。该测试 probe 的有界等待仅用于 headless 原生时序，不进入生产 GUI callback。

## 当前事实与首片范围

reference 只有一个 json_bridge 和它拥有的 task_service。两个 actiongen 动作是 reference.fixture.describe、reference.form.submit；页面通过普通 task.start/query/cancel/subscribe 使用 reference.summary handler。业务上下文存活到 Bridge 析构实际 join。当前公开 create_local_endpoint 返回同一 Bridge 服务上的 stopped 借用对象，成功创建即冻结注册及页面 capability 配置，首次 bootstrap 后创建会拒绝。

SouiGui 目前是页面内显式登记的 GUI 语义注册表。SDK Web client 只派发已订阅的 task.progress 事件，公开 webview_host 没有任意脚本执行或异步 GUI 命令入口。本地业务动作成功不能证明 Agent 已驱动三个页面的按钮、筛选、对话框、焦点或重载。首片只关闭真实公开业务组合：可选 native CLI、明确主体政策、两 typed action、一个 local-only summary family、真实文档撤销及退出。P0 的真正 GUI Agent 等价仍需下述后续契约和真实页面测试。

## 注册、启用与可信主体

默认不开端点。明确的 --agent-pipe <component> 与 --agent-allow-process <decimal PID> 才选择首片政策；两项必须同时给出，重复/未知/越界参数拒绝，与 smoke 模式组合也拒绝。component 遵守现有1..128 ASCII限制。名字是原生 UI/操作者分发的连接地址，不是授权秘密。页面、local wire 或配置中自报的 PID、SID、HWND、scope、gesture 均不能设定政策。

PID 参数只是可信操作者明确选择目标进程的输入。原生应用在启动时实际 OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE)，取得 GetProcessTimes 的 creation FILETIME 并持有该进程对象；无法核验、已经退出或预期非当前登录上下文即拒绝。不能仅保存 PID，也不接受命令行自报 creation/SID。每次 authorizer 接收 SDK 从实际已连接 server pipe 铸造的 windows_peer_identity，要求实际 PID、creation FILETIME 等于所持批准对象、两 held-process 状态 alive，并沿用 SDK 的 token/impersonation/SID/AuthLUID/session核验及实际 logon DACL。仅同账户/同登录、知道管道名或验证成功都不足以获准。每次重新连接仍运行该原生政策；不自动续期或重放。持有对象只用于核验，不授予其他进程操作权。

注册顺序为 register_business → 条件注册 reference.summary.local family及两 allow_local_action → 保持原页面 capability 白名单 → create_local_endpoint → Host start。factory 必须在 Host/首次 bootstrap 前，不能等 application_ready 才创建。endpoint.start 则必须等真实 application_ready、有效原生文档身份和会话关联均成立之后；bootstrap、DOM存在或静态诊断不代替这个条件。创建失败不启动 listener；任一配置失败沿真实统一关闭路径保留上下文。

原生 policy 只允许 bridge.describe、reference.fixture.describe、reference.form.submit及 reference.summary.local.start/query/cancel 与逐项对应 capability。不开放 generic task.*、续租、订阅、desktop服务、文件或任意 GUI。local policy 使用原生生成的独立 session/origin，不获取或发送页面 token。Agent 和页面共享服务、handler与固定样本，但使用不同实际 authorization scope；taskId/operationId 不能互相借用或跨新文档复活。首片不产生 native_user_gesture。

## 同一业务与安全结果

local_task_family_definition 固定 kind=reference.summary，复用已注册的真实 handler和两个现有worker，新增名字仅为 reference.summary.local。输入固定 fixtureId/category/durationMs，沿既有固定样本、500..60000ms及类别边界；输出 schema 只包含 fixtureId/processed/matched/totalSizeKb/groups 的有界安全字段。SDK 对所有 terminal outcome 运行 projector：实际成功时选取全部五字段，取消/失败时返回空安全对象，terminal state 仍来自实际服务；不转发 worker message、异常、原始私有字段或失败详情，不伪造 success。SDK 的 family scope/kind/operation 校验及统一容量继续生效。schema 本身不是业务授权。

人工页面与 Agent 用同一两 typed action及 reference.summary handler覆盖 fixed-summary、cancel、invalid-form、revoked-document。相同输入的业务结果应相同；不同入口的 scope/任务身份必须隔离。取消准入不是终态，operation未找到不证明未执行，任何 timeout不授权重试。页面 task.progress 仍是原服务的实际订阅；local首片用有界 query观察真实终态，不假设有local订阅或收到每个进度。

## 原生文档关联与严格退出

application/window/document/generation 都由原生应用生成、单调且不复用，不能取原始 HWND 数值作为身份。HWND重用或页面自报状态不能复活旧 key。保存原生 Miniblink execution_context_receipt，并在 owner 的 scope_check 复验 accepts_execution_context_receipt；Modern保存实际 adapter.session_digest 并复验非空、相等与 application_ready。host.session 的包装 ID不能单独证明浏览器当前文档。

新增 app-owned bridge_adapter 装饰器只代理现有 json_bridge，不新建 registry/store/service。它观察 SDK 调用的 shutdown_session：即使从engine/Bridge worker调用，也先原子关闭 Agent admission并调用线程安全 request_stop，再代理原 Bridge撤销；只 SetEvent 一个实际 manual-reset 控制 event，由现有 50ms owner pump 观察，不从外国线程读取或向可能重用的 HWND 投递消息。event reset 不能重开独立 atomic closed。跨线程不能调用 owner revoke_scope/pump/poll_stop，不留任何窗口/页面回调持锁。owner观察真实失效后使key永久无效，revoke_scope并poll_stop至真实stopped。Modern导航开始先清空实际session_digest；Miniblink旧receipt失效，因此后续owner准入不能仅依赖延后的通知。已准入handler不承诺回滚；停止后的结果不能再次发送。新文档不得自动恢复listener或旧授权，需新的明确native启用/批准流程。

关闭顺序为原子closed/撤销key → endpoint.request_stop → owner实际poll_stop到stopped → Host.destroy（E_BUSY保留全部对象与窗口并重试）→ Host reset → Bridge析构实际task join → Agent/业务上下文释放 → compat RuntimeShutdown →窗口class释放。窗口可保持closing计时器和控制泵，但不继续新业务准入。借用 endpoint 永不delete；poll_stop pending不等于Stopped，Stopped不等于任务worker已join。Bridge/Host失败或重复关闭不能跳过任何拥有者。app-owned policy、wrapper与业务上下文保留至其所有实际借用完成。

## 最小后续公开异步 GUI 契约（尚未实施）

复用当前 json_bridge event sink 的真实文档事件路由、同一个 task_service和 SouiGui 的既有 id/action/输入/状态描述。需要增加明确的公共 GUI 绑定入口，而不能把 task.progress 当伪命令，也不能直接操作 connection 私有监听器。json_bridge 公共可选 GUI模块注册一个固定SDK GUI task kind/local family；pending map是有界请求协调，不是第二任务服务或worker池。页面通过公共 client.bindGui(gui)绑定已经登记的 SouiGui。

原生接受 gui.perform 后只返回同一task_service实际准入的taskId/operationId。worker使用有界条件等待和协作取消，把不可变命令交给owner队列；UI不等待pipe、不嵌套泵、不执行私有eval。owner核对真实scope、窗口/文档身份、generation、期限及固定resource/id/action/capability/effect白名单后，沿已绑定的event sink向当前页面发送命令。最大16 pending命令、单owner派发槽、32KiB输入/64KiB输出和既有256-node/64KiB SouiGui snapshot上限；数字须在实施前由Root冻结，不能借此宣布全部M0预算完成。

命令带不承载权限的native requestId、原生document generation、resource/id/action及有界input；不带Agent bearer/SID/PID或页面凭据。公共SDK GUI事件必须有独立typed discriminator和严格字段校验，保留现有task.progress订阅行为。页面只对已绑定的真实SouiGui执行snapshot/perform/waitFor，handler进入同一人工业务入口；页面提交 page-only typed gui.ack，原生仅接受已有pending槽、当前实际page session/generation与唯一requestId对应的ACK。ACK不能创建请求、选择Agent scope或授权method，不能经local派发开放。未知、重复、撤销或晚到ACK丢弃并固定错误，不向后续文档发布。

native政策按登记的每个GUI resource/action及全部可能业务effect逐项批准，不允许以 gui.perform 一项概括所有页面权限。原生保留原Agent authorization scope，并在派发、业务effect桥接与ACK完成边界复验；实施必须给出明确的effect关联与method/capability交集，禁止让普通页面grant代替被拒的Agent权限。批准的固定GUI动作描述、effect表与其真实代码一起审查、固定hash；不接受页面自报的capability列表。局部输入约束和脱敏投影沿用已审actiongen/SouiGui契约，不能增加任意脚本、CSS selector、DOM抓取或用户gesture。

当前 task_invocation 只有 session/origin/operation/params，并不携带 authorization_scope；不能把这几个字符串当作scope或从app侧私读store。GUI实现需一个明确的实际scope交接：建议对SDK内部铸造的 task_invocation 增加只读 shared_ptr<const authorization_scope>，由原service在真实scope admission填入，worker得到同一不可伪造对象；页面/wire不能构造该字段。公开GUI模块仅持有这个对象并复验，其内部已注册family继续使用原服务品牌/容量。另一等价方案是在local family准入交接原生ticket；具体C++API、布局重构与normal回归须Root单独审后选择，不能据此声称当前API已支持GUI。公共页面入口拟为 client.bindGui(gui)，原生入口拟为 bootstrap前 json_bridge.enable_local_gui(...) 与owner实际 bind_gui_document(...)，均默认关闭、没有授权副作用。

页面 GUI handler 调用普通 client.task.start 使用页面 scope，不能代替原 Agent 授权。涉及 native effect 的动作需要 SDK 铸造、绑定原实际 AgentScope/document generation/固定批准 action→business family 映射的一次性 ticket；页面只带不可授权的关联 ID，不得 mint ticket、指定 scope 或扩大 task.*。真实业务准入必须由 SDK 消费该 ticket、复验原 scope 和 exact effect 后完成。ACK 只返回实际准入的子 taskId/operationId，使 GUI 父任务结束并释放 worker，Agent 仍按原 scope 查询子任务；不能在同一两个 worker 的池中让两个 GUI 父任务等待业务子任务终态，不能让 ACK 制造权限。重连后的新 scope 不得复活旧子任务 ID。具体 ticket API 未授权实施，首片没有任何 GUI command 或 page effect 桥接。

关闭/导航/断开/取消首先停止新准入并撤销pending槽，唤醒实际worker等待；已开始的页面业务不承诺回滚。ACK只证明实际注册页面handler返回，不是光学呈现、键盘/IME、任务join或权限发放证明。需要独立真实Host三profile的命令→页面处理→ACK→公开查询终态、撤销丢弃late结果测试，之后才能报告GUI Agent等价。此设计不改变当前SDK/API，也不宣称现有event/client已经支持它。

## 验证与边界

首片以当前已实际验证的normalSDK/公共factory6例、endpoint20例及reference业务4例为回归边界；本新组合仍需Root真实双架构构建与外部client进程测试。采用前Root运行本地Learning/cognition并复核当前源码约束；本FS lane不执行runtime/native工具。测试计划另列，全部releaseReady/qualification保持false。首片成功不关闭GUI等价、实际用户审批界面、中文IME/键盘、跨账户/session、Win7/PE、资源长跑、Modern完整树或发布门。

八个 headless case 使用实际 HWND_MESSAGE owner、公开 Bridge、实际 framed pipe 和相同 EXE 的外部 child，child 继承 Root 已分配 Job，显式 handle whitelist，不创建第二 Job 或 helper 服务。测试 predicate 仅说明该 native fixture 的窗口仍有效，不是假 GUI ready。每个 child 的原始输出绑定 CreateProcess 返回的 held PID/creation FILETIME，实际退出后才汇总；root 回执在 environment 析构、公开 Bridge 的实际 join 之后生成。任何仍存活的 child/异步 I/O 无法闭合都 fail-stop 并交 Root Job 保存失败清理记录。普通 GUI 接入仍需独立真实 ready、receipt/digest、撤销和退出证据。

## 局部安全结果合同与深度边界

SDK action_contract 的深度上限仍为8，不调整全局策略。原 groups array→items object→properties→group enum schema 独立深度为8；local query 在 result 下再包两层后深度10，不能注册。首片本地安全投影因此把 groups 改为固定 素材/项目/缓存/导出/归档 五键 integer object，各计数0..10000，所有五键必须存在且拒绝其他键；其 query 包装后深度8。成功仍投影同五个顶层业务字段，非成功仍为空安全对象。原 page handler 的五项 group/count 数组与全部计算保持原样。本地与页面的 JSON 表示不同，不能称逐字等价；native fixture 必须先核页面数组恰五项、每项只有 group/count、固定组唯一且计数有界，再转换为固定 map 并比较全部业务值。

selected-peer 在实际 normal SDK 上增加有限 contract parse/validate 回归：旧独立合同可解析、旧 query wrapper 拒绝、新 wrapper 接受，并拒绝旧数组/缺组/多组/负数/超限/字符串/小数。实际 Agent 构造仍通过公开 family 注册/原服务验证，未新增 task_service/store 或测试替代。此候选未执行；RED 保留原 Agent V1 以使修正注册合同后检查真实 prebind，GREEN 保留 V2 单调 binding latch 与原真实并发用例。八个 case 名、真实进程与所有资格边界不变。
