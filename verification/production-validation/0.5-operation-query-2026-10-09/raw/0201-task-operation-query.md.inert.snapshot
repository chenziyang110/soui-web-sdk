# 公共 task.query 的 operationId 查询迁移

已有 `client.query(taskId)` 和 `call('task.query', {taskId})` 保留，返回同一快照形态。调用必须恰好提供一个选择字段；过去被忽略的额外参数现返回 `E_INVALID_PARAMS`。taskId 与 operationId 均只接受 1–128 个 ASCII 字母、数字或 `-_.:`。取消、订阅仍使用 taskId。

启动回执丢失或超时后，在原授权 scope 仍活跃时，用已保存的 operationId 只读查询：

```ts
const operationId = 'import:job-42';
// 此前已经提交 task.start；没有收到回执不代表业务未执行。
const snapshot = await client.queryOperation(operationId);
// 与 query(taskId) 一样读取 queued/running 或实际终态，应用决定后续等待。
console.log(snapshot.taskId, snapshot.state);
```

`queryOperation` 不会重发 start 或自动 cancel。`E_TASK_NOT_FOUND` 可能是未接纳、其他 scope 或记录已淘汰，不能据此断言没有副作用。`E_UNAUTHORIZED` 表示授权不存在、失效或缺查询能力；应用重新授权后获得的新 scope 不能恢复旧任务的查询权。SDK 没有持久化操作日志或跨重连恢复服务。

Core 与兼容 MethodMap 使用 `SouiTaskQueryParams` 互斥联合；启用 `exactOptionalPropertyTypes` 时也拒绝给另一选择字段显式传 undefined。JavaScript 或宽结构变量仍须运行时校验。客户端将合法字段读取一次后复制为普通参数对象，拒绝额外字段、symbol 字段和自有 toJSON 钩子，原生 JSON 边界再次严格校验。

Agent 工具只从已授权 catalog 的现有 `task.query` 条目生成。当前 `soui.action/1` 不支持 oneOf 或 pattern，两个可选字符串的 schema 单独仍允许都不提供、同时提供或 Unicode；工具文字和运行时补齐 exactly-one 与 ASCII 规则。工具描述不是能力签发，不能由公开 scopeId 构造查询授权。

此变更针对 SDK 公共 task_service。Workbench 等应用自有同名适配器未随此切片迁移；消费者先确认实际宿主契约。未改变 Bridge 协议版本、后台线程池或 Windows 7/C++17 的声明目标，也未单凭本次无窗口测试完成目标机、GUI 或生产验收。
