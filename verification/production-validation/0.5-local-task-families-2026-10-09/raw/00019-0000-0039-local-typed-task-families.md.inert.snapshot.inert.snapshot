# ADR 0039：共享任务服务的本地 typed family

日期：2026-10-09。状态：accepted（实现与实际验证分别记录；本记录不代表已执行）。关联 ADR 0036、0037 与 0032。

## 背景

本地授权和 typed 动作派发已经同时检查实际连接对象、method 与 capability，但普通 task_service 只按 scope 归属任务。同一 scope 的任务不能因为拥有 task.query 或 task.cancel 就成为任意本地动作 family 的任务。输出 schema 也不证明原始任务 message、result 或业务错误适合公开。

## 决定

应用先使用既有 register_handler 注册一个有界 kind，再注册具名 local_task_family。family 固定一个 kind、name.start/name.query/name.cancel 三个方法、各自的 capability、输入和安全结果 schema，以及终态结果 projector。它绑定已有 kind 的实际 handler，不另注册业务 handler、不增加 worker 池。逐主体和业务参数授权仍由该 handler 明确实现；schema 和 capability 不代替业务授权。

family 是服务铸造的私有状态对象，带独立共享 issuer brand；不能用 this 地址、scopeId 或 kind 字符串伪造。服务开始销毁时先使 issuer 失效并设置 stopping，再 join 原 worker。注册动作只弱引用内部共享实现；注册表比服务长寿命时仍拒绝所有调用，不能通过残存 callback 访问已销毁的服务。

注册仅发生在 bootstrap 前。先解析全部三个动作契约，准备所有 callback、返回 handle 和两份候选 map，并检查重复名称、kind、数量与 schema。所有可能失败和分配均在 commit 前完成；最后仅以 noexcept map swap 同时发布 core family 与完整三动作。不向页面 capabilities 列表追加任何项。注册表 local_only 位同时阻断页面派发与发现；页面显式获得同名 capability 也不能调用这些方法。既有 plain task.start/query/cancel/subscribe/unsubscribe 不转换为本地动作。

本地直接 core API 与 transport 一样执行参数字节、解析阶段深度、JSON 数值、严格字段和 typed schema 界限。请求方法必须为 family 对应的固定方法。授权 scope 的只读 local-method 检查在其实际状态锁内检查 grant kind、连接对象活跃性、method、capability、期限和撤销；session/origin 也必须匹配。它不提供 bearer、scope 或 OS 主体认证的新入口。

start 在同一 core mutex 中核对活跃 issuer、实际 family 对象、已注册 kind、实际 scope 与 method/cap。任务 owner 在包括终态的整个保留期持有 family 与 kind。query 的 taskId 或 operationId 二选一查找，以及 cancel，均在该锁内重新核对 scope 指针、family 指针和 kind。普通页面任务没有 family 身份，即使 kind、scope 或 capability 相同也互拒。普通 task API 也不能读写 family 的任务或订阅其原始事件。

operationId 保持既有实际 scope 内的有界幂等合同。相同 retained operation、family、kind 和规范化输入返回同一任务；跨 family 或改变输入冲突。复用原 2 worker、32 inflight、1024 retained task 限额与淘汰规则，不增加永久 tombstone。not-found 不能证明没有执行过，也不授权客户端自动重试 start；恢复后的重试需要应用自行确定业务结果。

worker 完成业务时先规范化既有任务 outcome，然后在 core mutex 之外执行该 family 的 projector 一次。投影须是受限 result schema 接受的对象，缓存随原任务保留和淘汰。投影异常、非法或超界只记录固定安全投影失败，不改变真实 handler 的终态。query 不重新执行业务、projector 或任意业务 getter，只返回 taskId、固定 state、percent、十进制字符串 sequence，以及已缓存的安全 result 或固定投影失败码。query、cancel、进度都不返回原始 message、artifact、异常文本或业务错误。family 不提供订阅动作。

关闭、过期或撤销阻止新的 admission；出队以及事件 callback 之后仍按实际 local scope/method 重验。已经进入 handler 的工作不能回滚，取消是协作请求，终态保留真实 outcome。projector 位于 worker 生命周期内，服务 join 覆盖其借用数据寿命。端点发送仍需连接和文档代次串行复验；一次 active 检查不是发送屏障。

## 备选方案与迁移

registry、handler 与 event sink 使用不可变 shared callback 存储；候选 map 只复制字符串与 shared 引用，不能在 bridge/core 锁内执行业务 functor 的 copy 构造。新 callback/projector 参数在调用方构造，内部仅 move。registry prepare 持有私有 RAII mutation guard，所有注册/allow-local/nested prepare 在准备期间拒绝，防止重入改变候选来源。handler_for 的公开返回指针契约保持。worker normalizer 同时检查原始 message 的 UTF-8，进度先验证编码，emit 构造和发送均隔离异常，不能在业务 try/catch 之外结束 worker。

task_store 增加仅 task_service friend 可用的私有 start prepare/commit 事务。prepare 持有 store 锁、构造候选任务与两个 operation 索引、预备 snapshot 与被淘汰 ID；core 在既有锁内预备 owners、operations、queue、subscriptions 与完整返回 receipt。失败不消耗 ID/sequence、不淘汰旧任务、不留下孤立 queued 或 operation。所有分配完成后才用 noexcept swap 提交全部容器。公共 start_scoped 复用 store 事务并保持原返回、duplicate/conflict/capacity 和淘汰语义。

候选容器 copy 是正确性基线，上限仍为 1024 retained/32 inflight/128 subscriptions；每次 admission 会暂时同时持有新旧 map，锁内有界工作不等于低延迟保证。owner 的结果 cache 在 admission 时分配，候选只复制 shared 引用；worker 在完成锁内一次填充并发布终态，终态后不再修改。family cache 只保存已审核投影，不把原始结果变为可读取的共享字段。task_store 的既有 snapshot 仍按值复制，不扩大公共类型或另建 store。

私有 SOUI_TASK_FAMILY_TEST_ONLY seam 只在独立故障回归目标定义，以具名 prepare 点抛出受控 bad_alloc；正式 SDK 无 hook 或故障环境变量。不覆盖全进程 operator new，不声称重现 OS OOM。worker outcome 的数值、深度与 UTF-8 编码错误在 normalizer 内受控失败，不能逸出 handler 的异常边界而终止 worker。

不开放 generic local task.start/query/cancel：它们会把同 scope 的其他 kind 暴露给包装层。不另建任务池、authorizer 执行线程或持久去重存储。普通页面 API、已有 handler 注册、FIFO、续租与保留合同保持；新增公共 preview C++ 类型和内部共享实现布局需要消费者重新编译，不声称稳定二进制 ABI。

## 验证计划与边界

确定性原生回归覆盖完整三动作原子注册（重复、schema、具名准备分配异常无部分发布）、既有 callable 快照不复制与 bridge 锁内不发生 copy 重入、准备 guard 阻止各 mutation/递归注册、同 kind 页面任务与多 family 隔离、同 scope operationId 冲突、method AND cap、foreign service handle、服务销毁失效、严格输入与直接 core 预算、一次锁外安全投影及错误/敏感文本抑制、协作取消实际终态、续租/关闭/撤销/过期、出队后的再次检查以及共享容量/任务-operation-subscription 淘汰。页面订阅回归同时检查非法 UTF-8 terminal message 被规范化、两个 worker 的后续 FIFO 任务均实际进入并成功完成。同步使用 condition_variable 和两 worker rendezvous/drain，不以 Sleep 或析构取消猜测 handler 是否执行。

本切片不创建 pipe、认证 Windows 主体、实现 GUI/UIA、运行真实宿主、发布 SDK 或改变正式验收状态。编译和无窗口原生结果由后续冻结源码与独立 owned Job 原件证明；在执行前只能描述为实现与测试计划。
