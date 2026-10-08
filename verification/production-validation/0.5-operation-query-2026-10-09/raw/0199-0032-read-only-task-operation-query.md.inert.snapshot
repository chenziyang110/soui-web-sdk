# ADR-0032：按 operationId 只读查询公共任务

日期：2026-10-09。状态：实现与局部验证已完成，待独立审查及主线整合。关联：[ADR-0027](0027-native-authorization-scope-and-task-ownership.md)。

## 背景与影响对象

`task.start` 已把稳定原生授权 scope 与 operationId 组成索引，并对相同输入关联保留的原任务。若接纳回复丢失，调用者可能不知道 taskId；重发 start 不是只读查询，也不能证明此前是否执行。此切片只扩充同一个 `task.query`，复用现有 workers、owners、operations 和保留上限，不引入任务池、外部传输或恢复服务。

影响对象是 C++ task_service、公共 Core/兼容 TypeScript MethodMap、client 查询入口和已授权 catalog 的工具描述。结果快照保持原有 taskId/state/progress/message/sequence/终态 result；本次不改任务启动、取消、订阅、权限签发或退出规则。

## 决定

- 参数是 JSON 对象，且恰好有 `taskId` 或 `operationId` 中的一个字段。拒绝同时提供、都不提供、其他字段、null、数组、非字符串、空字符串、超过 128 字节或非 ASCII 标识字符。两个标识均仅允许字母、数字、`-_.:`。不裁剪、不转换、不静默丢弃字段。
- 原生查询先在任务资源锁内复验活跃的 `task.query` 授权。operationId 只与本次实际 `request.authorization->scope_id()` 组成 key；找出的 owner 仍须比较实际 scope 对象、session/origin、未撤销状态和仍被 task_store 保留的记录。公开 scopeId、页面 actor、旧 bearer 或其他 store 对象都不是查询凭据。
- taskId 的原调用保留并遵守相同归属检查。有效授权下未知、跨范围或已淘汰记录返回 `E_TASK_NOT_FOUND`；缺失、到期、撤销或不允许查询的授权返回 `E_UNAUTHORIZED`。查询不开始、不重放、不取消、不续租或重新授权任务，不改变进度、sequence、订阅或保留顺序。
- 续租保留同一活跃 scope，因此可继续查询原 operationId；重连获得新 scope 后不能查询旧连接任务。结果仅在现有进程内保留上限内可查；not-found 不证明该操作从未执行，也不授权自动重试。
- TypeScript 的 `SouiTaskQueryParams` 是互斥联合，两个 MethodMap 复用它。`query(taskId)` 保留，新增 `queryOperation(operationId)`；直接 `call('task.query', ...)` 和便捷调用经过同一校验并生成无 toJSON/额外字段的参数副本。无效输入在传输前拒绝，原生校验仍为最终边界。
- 工具定义只为原生 catalog 已出现且 capability 为 `task.query` 的无 action 方法提供 fallback；显式不可用方法不生成工具，catalog 缺少该方法时不注入。已有 action 描述保留。fallback 的 `soui.action/1` 受限输入 schema 使用两个可选、1–128 长度字符串和 `additionalProperties: false`，不增加 oneOf/pattern。该 schema **不能单独表达 XOR 或 ASCII 字符集合**；工具文字说明和 TS/原生运行时共同约束。描述不是授权证明。

## 行为矩阵与依赖

| 状态/输入 | 查询结果 | 状态变更 |
|---|---|---|
| 一个合法 taskId，实际活跃 owner | 原快照 | 无 |
| 一个合法 operationId，实际活跃 scope 下保留 | 同一 taskId 的快照 | 无 |
| 相同 operationId、不同 grant/store | 各自记录或 not-found | 无 |
| 同一 scope 续租 | 原记录仍可查 | 查询不触发续租 |
| 未知、已淘汰记录 | not-found，不证明未执行 | 无 |
| 缺/到期/撤销/能力不符授权 | unauthorized | 无 |
| 非 exact XOR 或非法标识 | invalid-params | 无业务调用 |

锁顺序、scope 分配器和 task_store 保留规则沿用 ADR-0027。操作索引仅定位候选，实际对象所有权必须复验；查询结果不形成永久操作日志。现有 Demo 类型文件有历史生成标记，本树无该旧生成器的默认输入；本切片只更新 SDK 交付声明，不改变 Demo 生成工具或业务适配器。旧 Demo 自有查询方法未承诺支持新形态。

## 备选方案

新增 task.queryOperation 协议方法会分裂授权和工具契约。重发 task.start 会把结果查询与可能的业务准入混合。按 bearer 或公开 scopeId 查找会破坏续租或实际对象归属。仅使用 schema 不足以表达当前受限词汇中的互斥和 ASCII 规则。上述方案均不采用。

## 兼容与迁移

已有 `{taskId}` 和 `client.query(taskId)` 的结果字段不变；以前被忽略的 task.query 额外参数现明确拒绝。丢失 start 回复后可以用原 operationId 只读查询，处理 not-found/unauthorized 时由应用决定业务恢复，不自动重发。`cancel`、`subscribe` 仍使用 taskId。本切片未实现本地 Agent 管道、scope-directed 事件、持久化恢复或跨重连查询。

## 验证与恢复边界

先保存新行为测试在旧实现上的失败，再实施和验证。无窗口 C++ 回归用 barrier 与受控时钟覆盖双 scope 同 operationId、同 scope 续租、跨 store、未接纳/排队/运行/真实终态、撤销/到期/淘汰、exact 输入拒绝及 handler 调用计数不增加；保留合法 taskId。Web 回归验证所有入口统一拒绝、查询超时后只读查询不会重新 start/cancel、工具 catalog 边界和受限 schema 的诚实限制；真实 tsc 检查互斥类型及原调用。

两架构原生编译使用 v143 和 Windows SDK19041，并保存实际命令/日志、源与 EXE 散列及 owned Job 原件。编译 root exit 与通用监督分别报告，不冒认编译尾进程例外；实际测试必须自然退出且 owned Job 归零。Web 执行 npm ci/build、相关 runtime/types 回归与 audit。所有失败保留，不覆盖 canonical 验收。此次不运行 GUI、VM、完整 Collect、真实八小时或目标系统兼容验收。

本切片已在 x86/x64 的 SDK 源消费者中编译并执行新查询回归（各 7/7）及既有 task_service、task_scope、task_store_capacity 回归；实际测试均自然退出 0 且 Job 归零。真实 Web 构建、互斥类型检查及六套相关运行时测试 83/83 通过、无 skip，npm audit 为 0。编译虽 root exit 0，通用监督的 compiler 尾诊断仍为 false，未改写或套用例外。旧实现的 Web/types 负证据和六项有效 native 行为失败均保留；另一次 public bootstrap 测试输入错误及早期验证脚本/编译 fixture 失败也保留，不能作为有效行为 RED。本次源码消费者不等同已安装 CMake 包、目标浏览器或正式交付验收。

回退此切片会恢复仅 taskId 查询；已有任务池和 scope 生命周期不变。证据完成后由 Root 独审、整合主线、更新最终交付身份和清单。
