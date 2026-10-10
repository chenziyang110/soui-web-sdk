# 0062 — Engine 固定 FIFO 与回复容量预约

状态：实现已采纳，2026-10-10 常规四配置集成回归通过，实际边界见文末。本文的 V2 候选 before 是原已封存 Engine 候选，基线为 ADR0061 metrics AFTER；原 Engine 与 metrics 历史封存保持不变。有限集成不代表长期资源或目标系统验收。

## 背景

长期验收计划要求队列和缓存有冻结上限。当前 Engine 的 `post` 使用无容量限制的 FIFO；Host 在 Bridge worker 已执行业务后才向 Engine 投递响应，不能保证响应有队列位置。短期 workload 或指标采样不能替代生产准入边界。

## 决定

每个真实 Engine 有一个固定 580 槽 FIFO。私有 `engine_thread::queue_capacity` 是物理槽数的唯一来源；friend `engine_admission::queued_limit` 从它派生，并静态检查四类额度之和。512 个 ordinary、32 个 control、4 个 renewal、32 个 cleanup 是排队额度；一个正在执行的回调另占一个执行位置。每类的已排队量与尚未提交的回复预约之和不能超过该类额度。额度不是优先级：所有成功投递仍按实际提交顺序进入同一个 FIFO，不插队、不重排、不抢占正在执行的回调。

`post` 和 `invoke` 的公开 bool 形式保持。SDK 私有 `engine_admission` 将 full、stopped、invalid 分开；没有网页选择 lane 的新字段或公开权限。cleanup 只用于已有 Host 关闭、终结 realm、revoke/drain 和销毁路径。控制与续期类别来自已有原生解析结果，不增加能力白名单。

Host 在业务 worker 入场之前，锁外准备 response holder、worker 和 resolver 全部闭包，再预约 Engine 响应额度。预约成功后才允许 worker 投递和执行。预约失败返回原生 `E_BUSY`，业务执行次数必须为零。预约本身不占 FIFO 顺序；业务产生响应后，已准备的 resolver 用预约额度提交一次。容量已预约的提交不会因后来的普通投递而失去位置；停止、导航代次或上下文改变仍可拒绝，不能借容量票据绕过原授权与严格上下文检查。

票据仅持有独立容量 ledger、Engine 启动代次和类别，没有 Engine 指针、业务回调、权限主体或 bearer。它不可复制，移动后旧票据失效，提交后失效。释放票据只原子归还容量；Engine 销毁后释放仍安全。旧启动代次票据不能在重新启动的 Engine 上提交。同一票据的可变操作由一个内部 owner 负责，不允许并发使用同一票据。

固定槽中只保存已准备的 `std::function`。入队/出队和丢弃用 swap，持锁时不运行、分配或销毁用户 capture。拒绝的闭包参数在锁退出后退休；`stop(false)` 的剩余闭包先从槽移到固定退休数组，锁外销毁后再 join。正在执行计数持续到该闭包 capture 的锁外退休结束，`drain_for` 不能先于 capture 释放报告静止。

stop 状态转换不依赖排队额度。cleanup 满时 Host 先关闭 gate，返回 `E_CURSOR_CLOSE_PENDING`，保留 Host/adapter/Bridge/真实 HWND/模块和未完成的原生清理所有权。调用者依公开约定在原预算内泵实际 owner 并重试 destroy；此切片不强制释放或延长预算。cleanup 有空位仍按 FIFO 等待，不能抢占无界阻塞业务。

metrics 默认 disabled 仍返回原 disabled/全零快照，包括新增 `queue::full_rejections_total`。scalar 模式下，普通/内部 lane 的 `post_internal` 实际满额拒绝与 `reserve_reply` 实际满额拒绝各只累加一次；计数在同一 Engine 锁内用原饱和 helper 更新。事件的 ordinary 投递也经过该通用 post 分支。stopped、invalid、旧代次、重复票据或成功提交不算 full；此字段是一次 admission 尝试的计数，不是业务请求去重计数。每个 Engine 自己计数，Bridge worker 与共享 process Engine 分别采样，不合并为单 Host 总量。getter 不触发业务、授权、入队或清理。scalar getter 使用已有 `has_capacity_bound` 和 `capacity_bound` 表达实际 580 排队上限，原排队/执行/时间/高水位/停止拒绝含义保持。预约不是 queued。原 11 个 metrics 用例 ID 保留；容量与默认关闭检查明确增加新字段。

## 兼容性与边界

Engine 私有布局与 V2 的公开 queue 诊断 POD 布局改变，SDK 与消费者必须一起重编；不声称旧二进制 ABI 相容。新增标量没有新增 callable 或 wire 方法。Host 在本切片仅增加 private friend 接入，不增加字段、public callable 或 virtual 槽；metrics 基线的布局变化仍适用其迁移要求。不增加安装后的 lane/ticket 公开 API，不改变 Bridge JSON、30 分钟租约、权限存储、本地 Agent 协议或 task_service。Permission 的独立额度另切片处理。

固定容量不是性能验收。执行中的 callback、初始化/关闭、native message callback 和同步 `future.get()` 不可抢占；原 Owned 超时监督仍必要。`invoke` 超时不撤回已接受工作。Promise/JSON/script 执行中的分配和厂商 GC/OOM 恢复未因此合格。事件入队 full 时保留固定失败诊断，不自动重放，也不保证无丢失。

## 验证

新增回归必须链接正常 SDK 中的真实 `engine_thread`，通过实际线程、事件、Promise、捕获对象析构和 FIFO 顺序观察以下边界：580 排队加一个实际执行；四额度拒绝；停止不依赖队列；回复预约、移动、退款、单次提交和旧代次拒绝；拒绝与丢弃的析构能重新读 Engine；超时工作不被假取消。

实际 Host 回归必须使用真实 SDK、kernel、Bridge、资源和 owner HWND，验证满额预约时业务零次；cleanup 拒绝时实际 owner-only metrics 读取 `bridge_accepting == false`，原完成 receipt 的实际 getter 拒绝且携同一旧 receipt 的真实请求返回 `E_CONTEXT_PENDING`、业务仍未增加。已有 owner-only `host_engine` 必须返回原 Engine，联合其实际 webview/cursor/owner 非空检查与 HWND/PID/thread/module 检查证明保留边界。各快照非原子组合，不能扩展为厂商所有资源证明。释放实际阻塞回调后原 destroy/drain/join 必须自然完成。缺 kernel 不能把该项标为通过。不得用测试替身实现调度或授予票据权限。

常规入口已完成 x86/x64 的 Debug/Release 实际编译与执行，各 29 项测试通过并独立复核，见[有限整合回归](../../verification/production-validation/0.5-runtime-capacity-2026-10-10/README.md)。此前真实 Host 安装消费者的有限验证另有记录；最终组合的安装消费者、长期 8 小时、资源趋势、完整工具链、目标系统和正式发布仍待验收。
