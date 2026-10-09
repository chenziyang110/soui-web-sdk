# 本地 typed 任务 family

这是 preview 原生包装 API，基于 [ADR 0039](adr/0039-local-typed-task-families.md)。它不创建本地端点、认证 Windows 主体或开放 GUI 自动化。实际端点必须先验证主体与业务授权，私有持有连接 binding/bearer，并在发送前串行复验连接与文档代次。

先通过 `task_service::register_handler(kind, handler)` 注册已有业务，再在 bootstrap 前调用 `register_local_family(registry, definition)`。`json_bridge::register_local_task_family(definition)` 提供相同的受控注册入口。definition 指定 name、固定 kind、start/query/cancel 各自 capability、对象 input/result schema 和安全 projector。方法固定为 `name.start`、`name.query`、`name.cancel`；`task` 名称保留给 plain 内建方法。三项发布是同一个准备/提交事务，重复、无效 schema 或准备异常不留下部分方法。

新增动作始终 local-only；没有页面 capability 自动新增，页面显式授予相同 capability 也不能调用或发现它们。原 plain task 方法不会变为本地动作。family 使用服务发出的实际不透明对象，kind/name/scopeId 字符串都不是替代凭据。业务 handler 与页面既有 kind 完全相同；应用必须审核该 handler 是否适合主体及传入的业务参数，typed/schema 不替代这个审核。

直接原生请求通过实际 local scope 的 method AND capability 复验。示例中的 schema 与投影仅示范形状，不代表特定业务已获授权：

```cpp
soui::runtime::local_task_family_definition definition;
definition.name = "app.work";
definition.kind = "work"; // 已注册的业务 handler
definition.start_capability = "app.work.start";
definition.query_capability = "app.work.query";
definition.cancel_capability = "app.work.cancel";
definition.input_schema_json =
  R"({"type":"object","properties":{},"required":[],"additionalProperties":false})";
definition.result_schema_json =
  R"({"type":"object","properties":{"succeeded":{"type":"boolean"}},"required":["succeeded"],"additionalProperties":false})";
definition.project_result = [](const soui::runtime::task_outcome& outcome) {
  return outcome.kind == soui::runtime::result_kind::success
    ? std::string{R"({"succeeded":true})"}
    : std::string{R"({"succeeded":false})"};
};
auto family = tasks.register_local_family(registry, std::move(definition));
// 检查 family，再由已认证端点明确授予所需 method/capability。
```

start 参数为 `{operationId,input}`，kind 不从请求读取；query 为 `{taskId}` XOR `{operationId}`；cancel 只接受 `{taskId}`。未知字段、非 ASCII/超长 ID、超界类型、超过 32 KiB 或解析深度上限的 JSON 都拒绝。query 的发现 schema 不能表达 XOR，实际 core 仍强制二选一。所有查找和取消在同一任务锁内核对实际 scope、family 与 kind；同 scope/same kind 的页面或其他 family 任务互拒，包含终态与 operation 查询。

query 只返回 taskId、固定 state、0–100 progress、精确十进制字符串 sequence；终态附一次缓存的安全 result，或固定 `projectionError=E_RESULT_PROJECTION`。没有原始 message、业务错误、异常文本、artifact 或订阅入口。projector 在完成 worker 上、服务锁外运行一次，必须有界且不得保留 `task_control`。投影非法/抛异常不会把真实 success/partial 改成 cancelled/failed；正常 handler 输入错误和无效原始 JSON outcome 仍走任务的失败语义。重复 query 不重新执行任何业务或 projector。

取消只提出协作请求，已进入 handler 的副作用不能回滚。关闭/过期/撤销后新 admission 被拒，尚未执行的 queued 工作再次复验；已执行工作保留真实终态，连接关闭可能使它无法再查询。先精确撤销 scope，再调用 `shutdown_scope` 进行资源清理。服务销毁先失效 issuer，再 join 共享 worker，持有旧 family 或旧注册表不能复活 authority。应用借给 handler/projector 的数据须活到服务 join 完成；注册阶段不能与派发、注册表修改或销毁并行。

池和保留仍共享既有 2 worker、32 inflight、1024 retained task。operationId 仅对保留中的同 scope/family/kind/规范化输入去重，改变输入或跨 family 冲突。任务淘汰、进程重启和恢复可能得到 not-found；这不证明任务未执行，不授权客户端自动重试 start。没有永久去重或持久 tombstone。

准备/提交使用有界候选容器 copy；owner 共享结果 cache，终态后只读。1024 上限下仍有峰值内存和锁开销，尚无性能或真实宿主结论。新增公共 preview 类型需要重新编译消费者。实际验证待冻结输入和独立双架构 owned 记录，本说明不宣称已运行、Win7/PE、pipe、长稳或发布通过。
