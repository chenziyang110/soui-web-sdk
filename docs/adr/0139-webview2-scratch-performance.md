# ADR 0139：WebView2 所有者批次与 Scratch 计量开销

状态：本地实现候选，未编译、未运行或宣称性能通过。日期：2026-10-29。

## 背景

重复 show/resize 会提交相同 COM 状态；Bridge 回复虽有 16 项上限但没有时间上限，且 Host 锁内释放请求 lease 会嵌套进入预算锁。Scratch 计量每秒及外部 snapshot 都复制、排序整个间隔数组，2000 个样本又不足以完整覆盖 30 秒高刷记录。

## 决定

- Controller 状态只在成功提交后缓存；相同状态不再提交 COM。失败、重入中的不确定状态保留重试机会，缓存只属于该 Controller 生命周期，不缓存网页身份或授权。
- show/resize 在调用前分别保留强 state 与 Controller 引用；初次 mount 同样保留 Controller。即使 COM setter 重入导致 adapter 销毁，返回后的 cache 写入仍有其所属对象存活。mount 的资源映射和首次 Bounds 返回后还检查 prepared phase 与 controller/core/core3 身份，取消时直接短路，不再挂 Bridge/worker 或复活 closed 状态。缓存去重不能代替 mounted/owner STA 检查。
- UI STA 的回复 drain 同时受 16 项与 2ms 合作式预算约束，第一项始终允许；单次 COM/JSON 操作不可抢占。保留原 lane 公平性、逐输出 document/generation/source 校验和剩余输出合并通知。请求 lease 在 Host mutex 外释放。
- Meter 以有界容量覆盖默认 30 秒、最高声明 1000Hz 的间隔。记录期间仅返回便宜的计数快照；完整分位数在结束后计算并复用，避免轮询 snapshot 排序。超出容量必须显式报告，并不作为完整窗口对照有效样本。
- 演示面板只在可见时写变化值，隐藏面板不影响计量生命周期。逻辑工作量由现有 Runtime 事件计数；无法取得的维度标未支持。
- 新增 `measurementVersion:2`，主回调率 `windowRafFps` 使用完整实际墙钟窗作分母，包括首尾无回调时间；保留历史 `rafFps` 为闭合间隔节奏。开始前声明 `expectedFrameRate`（默认 60，30..1000），两帧作为边界覆盖预算，写入 `boundaryBudgetMs`。首尾超过预算或零回调时标 `censored`，主分位数不可用，已观察分位数独立保留供诊断。真实卡顿与零帧采样仍是有效的坏成绩，不以环境失败丢弃或重试。
- `validForWindowRateComparison` 只说明可见、完成且有正实际时长；`validForComparison` 另外要求非 censored、完整间隔容量及至少两个闭合间隔。Electron / 原生 W 工具分别保留采集有效与性能／分位数资格，不将前者当作性能达标。
- Source capability 的计数租约按 Runtime 共享；多 meter 重叠采集时最后一位 observer 才清理由 meter 创建的计数对象。外部持有的计数对象不清除。

## 兼容、备选与迁移

不更改公共 SDK API、资源授权、COM STA、退出协议或图形后端。未选择跨输出缓存 Source、迁移 COM 到 worker、强开 GPU 或用 browser flags 换帧率。Meter 继续输出 v1 schema 并保留旧字段的 cadence 含义，以 measurementVersion 区分新计量；运行中分位数允许 null，结束报告仅在已声明边界覆盖完整时提供主分位数。历史报告不改写或补造缺少的完整时窗数据。无新增运行依赖，产物是否零增长仍由共同体积门禁决定。

## 验证与回退

新增原生策略回归覆盖成功去重、失败重试、重入缓存失效与时间预算；JS 回归覆盖高刷完整窗口、snapshot 不排序、容量溢出、逻辑计数、重叠所有权、首尾停顿与零帧保留。强 state/COM 对象存活由生产调用处静态检查，实际销毁重入仍须原生验证，策略单测不能代替。此阶段只补代码与静态检查，专人集中编译/运行。出现回归可分别回退 Controller 去重、drain 预算或新版 Meter，保留同负载和版本身份。
