# ADR 0037：本地连接复用公共动作派发

日期：2026-10-09。状态：accepted（原生派发 API 切片；实际端点与主体认证尚未实施）。关联 05-07、05-08c；以 ADR 0036 的实际连接对象与授权范围为前提。

本切片提供原生调用边界，不创建命名管道、认证 Windows 主体、启用 GUI 自动化或建立新的任务服务。实际端点必须先验证并授权主体，绑定窗口/文档代次，且在共享服务销毁之前完成撤销与 I/O 清理。

capability_registry 增加原生注册阶段的 allow_local_action(method)。它只允许已有的 soui.action/1 typed action；硬拒绝保留方法 bridge.describe、bridge.session.renew、task.subscribe 与 task.unsubscribe，发现列表也排除这些名字。所有既有动作默认不向本地开放，普通方法不会因 capability 被授予而自动开放。json_bridge 提供相同的受控注册入口，bootstrap 后拒绝修改。这个声明不添加页面 capability、不认证业务字段是否敏感，也不替代逐连接 authorizer。应用须明确审核该动作的名称、描述、输入、输出和业务实现是否适合被授予的主体。

bridge_transport 增加 handle_local 与 describe_local，接收实际 native_connection_binding 和仅在原生端保存的 bearer。传入请求的 session、origin、token、authorization 和 native_user_gesture 均被重写；session/origin 来自不可变绑定，gesture 始终为 false。页面入口仍仅接受页面 grant，不能因新增原生入口而导入本地连接授权。

两种入口共用参数界限、动作输入校验、业务 callback 和输出校验。执行前 resolve_local 同时检查实际 store/连接、scope、bearer、method 和 capability；本地未知、未开放和未授权方法统一拒绝，不披露注册是否存在。这里的直接调用不排队；未来 owner_executor 必须在真正执行边界调用本接口，并额外复验窗口/文档代次。关闭在授权检查之后发生时，已开始的 handler 仍可能执行，不能承诺回滚或完成屏障。

参数和页面 envelope 在解析阶段执行深度界限，避免先构造超深 JSON 再检查；不可表示的数值与其他 JSON 值异常返回固定 schema 错误。typed action 输出沿用解析阶段深度界限，并捕获同类数值异常。业务 callback 不会收到被这些检查拒绝的输入，错误响应不包含原始异常文本。

本地 bridge.describe 是固定的只读发现入口，不调用应用业务 callback。它要求 canonical bridge.describe capability 与实际本地 method 授权，按 local-enabled typed action、method 与 capability 过滤后才分页和计数。输出仍使用既有 capabilities/action 描述；隐藏方法不影响 cursor。返回前复验实际授权，不能把过期或关闭后的快照发出。描述字段仍是应用明确审核的公开数据。

bridge.session.renew、task.subscribe 和 task.unsubscribe 不通过本地业务派发开放。未来端点持有私有 bearer，直接使用 lease_local/renew_local，且不得把新 bearer 放入 wire、发现或日志。当前 json_bridge 在构造时已将 task.start/query/cancel 注册为 plain 方法，重复注册 typed action 会拒绝，allow_local_action 也拒绝 plain；本切片没有转换这些内建入口，它们继续全部不可本地调用。未来安全 typed task wrapper 必须限制 task kind，查询与取消必须核对实际任务 family/kind，并复用同一 task_service、显式提供适合主体的输出契约与投影；scope 校验和 output schema 不能替代这些限制。task.query 保留已有 taskId/operationId 二选一的原生校验，查询不创建任务或重新执行。本切片不实现该任务包装层。

本地业务失败不返回任意 handler 的错误码、消息或异常文本；输出结构校验失败也不会把原始结果发出。正常结果只有经过同一受限 output schema 的字段可返回，但 schema 只验证结构与界限，不证明字段不含敏感信息。返回前再次检查绑定与授权；端点最终发送边界仍须串行复验连接和代次，单独的 active 检查不是发送屏障。

验证覆盖默认不可见、显式 typed opt-in、方法与能力交集、复制 bearer/foreign binding/page-local 互拒、输入/输出非法、业务异常与错误文本隔离、关闭/撤销/过期及发现分页。页面动作、续租、任务 scope 与 operation 查询回归继续保留。最终以两架构实际静态 SDK/所选原生编译及 owned Job 原件为证，不以 authored fixtures、headless consumer 或受控时钟宣称管道、GUI、Win7/PE、长期运行或正式发布完成。

当前 Win11 已实际完成 Release x86/x64 静态 SDK 与所选七组原生测试，各组CTest7/7、派发15/15；八个Job自然结束，68项实际输入与输出身份保持一致。[验证原件](../../verification/production-validation/0.5-local-dispatch-2026-10-09/README.md)保留编译/测试记录、精确进程身份、原始输出及首次额外审计夹具失败。该证据绑定这次实际输入；后续入口和源码组合不自动成为完整发布验收。
