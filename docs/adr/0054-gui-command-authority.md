# 0054：GUI command 与一次性原生 effect 授权交接

状态：已采纳并完成有限源码与协议验证。正常 SDK25 已在 x64、Win32 两架构重新编译；原生用例自然通过，但两个编译阶段的后台辅助进程均需有界清理，编译资格仍为部分失败。使用 Windows7 API 范围、SDK19041、C++17；目标系统支持仍需实际验收。不新增 renderer/Host 接口、外部 helper 或线程池。

## 背景与决定

人工页面已有 SouiGui、typed action、task 服务。Agent local 请求拥有另一实际 authorization_scope，页面 task grant 不能代替它。八项 headless Agent GREEN8 证明有限本地业务组合，不证明真实 GUI。采用一个可选 json_bridge-owned coordinator；默认不开，固定注册表最多16行，各行对应独立 local family 和准确 native resource/id/action。使用同一 permission store、registry、transport 和 task_service 的两个 worker/32 inflight。

公开 task_invocation 的四字符串布局保持不变。SDK 私有 job context 保存 transport 实际解析出的 Agent scope、family 品牌、不可重用 document/request IDs、固定 canonical input/单次 effect 和 deadline。context/slot 在实际 task admission 前预备，成功时随 job 一起提交；失败/重复不留下额外 slot。普通 task.start 不启动 GUI kind。

三项 page-only gui.bind/effect/ack 由正常 page grant 解析。page 与 Agent scope 对象必须分别匹配；requestId/documentId/effectId 只是 correlation，不承载 authority。Agent 同时批准 GUI method/capability 和实际 effect method/capability；page 不能指定 method/scope/kind/operationId 或 mint ticket。effect prepared→consuming 单次提交，参数与原批准 canonical input 完全相同，native gesture 始终 false。异常/timeout/取消后不重试或回滚已开始效应。继续使用现有 action_contract depth8、local E_ACTION_REJECTED redaction 和真实 task family 归属检查。

native bind 在实际 ready/session/compat receipt 或 Modern digest 后由 owner 提交。原生 current callback 在锁外仅 owner 执行，发布/派发前后复验；它不能由页面 ready 布尔替代。初始已完成 prebind shutdown 不永久关闭，可信 binding 一旦开始则 atomic 单调关闭，撤销不重新开放。一个 owner 派发槽，不在 pipe/UI 线程等待页面或 callback。取消/关闭在 task lock 内仅 SDK noexcept atomic close+CV notify，不锁 GUI 或执行应用 callout。

GUI event 沿现有 renderer session/generation 绑定的 sink。页面实际 SouiGui handler 使用一次命令专有的 effect closure；人工 unary handler/perform 行为保留。ACK 校验实际 page scope/document/唯一请求/期限。SDK 保存的实际 child receipt 不接受 page ACK 覆盖；GUI/只读结果经过固定 schema 与 native projection，secret 字段/任意 DOM/异常原文不返回。完整 event/response/query wrapper 有界。

首片具体结果为 confirmed、guiResultJson、effectState、effectResultJson 四个固定字段。两项 Json 是经过解析/UTF-8/深度/schema/投影检查的 JSON 字符串，避免 task.query 再包一层时增加 schema 深度。SDK 对完整 result/query 和普通 transport 最终 envelope 分别检查；转义后的 result 超过30,720 bytes 就返回 failed、confirmed=false，若 effect 已消费则标 unknown、不给重放权。committed 表示实际 typed handler 成功返回或真实 child admission，仍不表示 child 完成。redacted error/异常/期限内未返回只能 unknown，不能证明没有副作用。

GUI effect 仅实际 owner 执行；已认证的 Bridge worker effect 请求进入最多16条的私有队列，由既有 owner poll 在原期限内执行；其他 foreign 调用仍拒绝。worker 不执行窗口或 Host callout，owner 不等待 worker；队列授权、取消和单次消费细节见 ADR0058。bind/poll 与实际 effect callback 都有原生 borrow：重入 poll 返回 busy，关闭后尚有 borrow 返回 pending，全部 owner callout 结束才返回 closed。取消 hook 不锁 GUI/context；worker 至多每25ms复查 atomic/CV，覆盖无锁 notify 与 wait 的交错。projection 在 worker 上执行，禁止捕获 Host/窗口；业务存储仍必须活到 Bridge 实际 join。GUI event 只派发已实际进入 worker 的 context，未执行取消不会消费 effect。

dispatch 通过不续期 effect 权限。effect borrow 保留 actual current callback，并在消费前、紧邻实际 typed callout 前及返回后于锁外 owner 调用；每次前后复验原 document、closed、期限和真实 Agent/page scope。proof false、异常或重入关闭阻止尚未开始的 handler；已消费后任何失效保持 unknown，不回滚或重试。owner-reentrancy 实际回归覆盖 dispatch 后 proof false 的零副作用，以及 proof 内关闭返回 pending 后零副作用。

两个 GUI parent 可以使用现有 worker 等待 bounded ACK；start effect 只等待真实 child admission。ACK 后 parent 返回释放 worker，Agent 再以原 scope 查询 child，不能让两个 parent 等同池 child terminal。GUI parent success 不代表 child success/呈现/native join。丢失 ACK 保留真实 effect receipt或unknown，不能据 not-found 自动重放。

coordinator 声明于 tasks 之前，destructor admission close→endpoint 实际 stop→tasks 实际 join→coordinator/业务存储释放。owner callback 尚借用时 close 返回 pending，保留 Host/存储；closed 后才销毁 Host。没有 detach、伪清计数或重入嵌套消息泵。

## 预算、兼容性与迁移

固定16 rows/16 pending、一个 owner slot、每 command至多一个 effect；32KiB输入/投影结果、64KiB完整envelope；native deadline1..10000ms，默认10000。保持现有 family64/task32/worker2、SouiGui256nodes/snapshot64KiB。数字只属于本模块，不证明M0全树/cache/Modern已完成。

Web route 的 pending16、每 document 256个已见 request ID 是有界 replay ledger；满后关闭且不驱逐旧 ID。dispose不撤销已开始的业务，也不重新 bind 同 client。人工 handler 仍只收到一个参数；native command 才收到可选 command context，effect closure拒绝 stale/重复且绝不改变普通 client 的 page token/scope。所有原生权限检查独立于这个 Web ledger。

公共 C++ API 为 enable_gui_commands、bind_gui_document、poll_gui_commands、close_gui_commands；Web新增 bindGui 和可选第二 action context。旧人工 handler、public task_invocation、正常 task 与 local error policy 保持兼容。json_bridge私有布局和正常SDK源改变，必须重构正常archive/安装消费者；不借旧SDK运行结果。支持compat代码仍需真实typecheck与三视图build，不能因新语法提高目标。

备选方案：直接用页面 grant 执行Agent业务会混淆权限；透传Bearer/任意eval泄漏权利；第二task服务造成双重生命周期；GUI父任务等待同池子任务终态有死锁风险；因此拒绝这些方案。

## 验证与事实边界

normal Soui::Sdk十case覆盖registration/scope-matrix/one-use-effect/actual-child/owner-reentrancy/revoke-prebind/timeout-consumption/capacity-secret/two-parent-pool/external-peer。原生服务fixture的page ACK是明确协议边界，不能填真实DOM/GUI字段。Web pure覆盖parser/session/sequence/late/dispose/reentrancy/旧handler兼容。Root另行真实两arch CTest/direct/install消费者，之后完整reference三app及人工/Agent四业务流程。GUI handler ACK、typed effect receipt、actual child终态、当前receipt/digest与actualclose/join均需真实证据。

Root 已保留独立原始记录：SDK Web 57 项逻辑测试；GUI 原生十用例在两架构分别直接执行与 CTest 通过；包含 Endpoint、factory、GUI、Host、UIA 的正常 SDK25 合并回归在两架构各有 47 项直接执行及 47 项 CTest 通过。参考应用另有 51 项前端测试、三视图构建、两种 classic 转换与两个固定锁依赖审计通过，原生业务 4 项、Agent 8 项及 GUI 注册 2 项在两架构分别直接执行与 CTest 通过。这些是分开的有限验证，不合并成一次全 SDK 通过；参考应用的新私有观察源码尚待三应用实际编译与运行。

configure/build 的 root 退出为 0，但辅助进程未自然闭合，supervisor 为 124；其原始失败记录和 generic=false 保留。安装消费者、实际 GUI 等价、IME/输入/物理呈现、刷新率、目标 OS、长期、完整交付和 releaseReady 仍 pending。只读观察或协议 fixture 不替代这些验收。

后续验证：ADR0058 的 owner 交接修复已采用，新增四项回归使 GUI 原生集合增至14项；x64、Win32 各14项直接执行及14项 CTest 自然通过。当前 x64 React、Vue、现代后端的真实 Agent GUI 各完成9类、13次操作，表单有实际 committed 收据，子任务有实际 cancelled 查询终态，应用及其自有进程自然退出。现代后端另有真实浏览器 NORMAL、signaled、订阅撤除与 callback0 的退出观察。源码、构建及运行记录详见 ADR0058；编译辅助进程 0/124 原始失败、物理输入、性能、目标系统、长期和正式发布边界保持不变。
