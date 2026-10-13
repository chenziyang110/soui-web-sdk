# ADR-0074：有限命名缓存配置检查与重载校准采样

- 状态：proposed
- 日期：2026-10-13

## 背景

216 字节可选快照已报告 Blink MemoryCache、Skia attached glyph 和 retained resource 标量及实际配置，但长期分析只描述这些数据，必需 `cacheBudgetGate` 始终未评估。Blink capacity 是软清理目标；discardable resource 的 total/raw single 为零表示没有显式字节上限。命名配置匹配不能替代整个缓存或 heap 预算。

## 决定

保留原标量解析 API，增加独立的配置一致性、retained-record 清理约束、软目标比较和完成副本覆盖子结果。使用规范 uint64 字符串及 BigInt 精确算术；已知矛盾或狭窄约束违反为 failed，缺失、partial、unsupported、stale、pending 或退休副本不获得新鲜采样资格。

完成副本 age 及相邻/小时边缘覆盖沿既有 180000 ms 门限，每完整小时至少 30 个不同的新完成 QPC。该时间仅代表完成副本，不声称组件同一时刻采样。退休后保留原副本和 QPC，不要求进程缓存归零。经验字节预算仍为 null，未覆盖范围仍显式存在，因此配置通过不能把整体 cacheBudgetGate 放绿。

长期配置子门仅聚合 eligible fresh complete 点；其他点保留为诊断并从覆盖计数排除，不把 partial 变成 pass。每小时覆盖仍必须独立满足完整副本数量/间隔，已知矛盾或 retained 违反进入必需子门并优先 failed。资源校准则严格要求 cycle10–110 全部 101 个点均完整新鲜。最终显式 captureGate 合并有限缓存子门与原数学门，未通过则入口非零；原数学门字段与默认调用不改变。

独审发现缺失 external binding 的提前返回曾掩盖当前已完成 DTO 的 ABI、uint64 与 retained 配置矛盾。修正先检查当前版本自身约束，再判断外部 subject/module/clock 与新鲜资格；合法数据缺绑定仍未评估，不升级为数据失败，也不获得资格。任何已知矛盾即使同点缺 clock 或其他点的小时覆盖足量，仍必须 failed。原 v1 源封存和五例失败复现保留；修复验证另存 v2。

公开 `Test-Acceptance.ps1 -ResourceCapture -CacheObservation` 沿原 100 次重载、10 次预热、5000 ms 静置、20000 ms 步骤门限和原 Owned 预算，显式加入已有 Native 参数 `--cache-observation=1`。新选项只用于资源采样；新策略修订、request、file bindings 和实际 argv 必须一致。未选择时保留原策略字节和默认参数。两种 8h 路线保留原必需采样，不修改时长、循环或指标。

## 兼容性与边界

不修改 C++、216/168/264 字节 ABI、缓存限额、purge/GC、控制器、依赖或版本。总 retained bytes 不与单次分配上限比较；Blink/glyph 软目标超出是合法数据。discardable count 上限不产生字节预算。配置变化不允许偷偷扩大已校准分母；原始点与变化记录完整保留。

当前 SkResourceCache::purgeAsNeeded 在 discardable 分支以内部 SK_MaxU32 sentinel 表达无配置字节限制；本门不把这个内部实现常量新增为公开预算或硬编码验收不变量，仅检查已发布阈值的选定狭窄规则。全 image heap 与外部淘汰数据仍未覆盖。

既有纯数据调用允许不传 `completeResourceAnalysis` 的 records，而原 `.some` 会抛错。缺省改为空数组，仅返回 Native/model/resource/calibration 未评估、passed=false；不借空数据提供资格。实际文件入口仍传入本次完整原记录并做全部最终绑定，缓存分析也从这些记录重新计算。

新 100 次采样只提供后续冻结经验预算的当前数据，不自动证明 Native ABI、完整缓存覆盖、8h、OS 或发布资格。原失败和旧策略 receipt 不升级。

## 验证

追加纯数据边界回归，覆盖精确数值、配置/目标语义、未知状态、新鲜和退休副本、缺校准及公开 opt-in 的端到端选择/绑定。执行 Node 纯数据、源码和 PowerShell AST 检查。Native 编译、真实当前校准和 8h 均由维护者后续独占执行；本切片无实际 Native 资格。
