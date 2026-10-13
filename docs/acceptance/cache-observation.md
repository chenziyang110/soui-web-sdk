# 可选命名缓存快照

此接口采集有限组件标量，不提供内存合格结论。原 renderer v1/v2 仍分别为 168/264 字节；新 `wkeGetCacheDiagnosticsApi(1)` 表只由支持该接口的已加载 DLL 提供。cache snapshot 为 216 字节，调用方必须使用完全相同的尺寸；错误版本返回空表，错误尺寸返回 invalid_argument。

SDK 调用 `request_miniblink_cache_sample()` 在当前 Engine ordinary lane 提交单个异步采样任务；已有任务在途时返回 pending。以后调用 `read_miniblink_cache_metrics(out)` 只读取完成副本。读取不创建缓存、不投递、不等待、不泵消息，不保证取得当前 UI 事件的新样本。关闭开始阻止晚到结果写入，并保留最后完成样本及原 `sampleQpc`；不得把 UI capture 时间冒充采样时间。模块路径匹配仅证明加载路径，DLL 散列必须由调用者独立绑定。

| 命名范围 | 实际字段 | 配置含义与未覆盖范围 |
|---|---|---|
| Blink MemoryCache | live/dead bytes、capacity、min/max dead capacity | 当前软 prune 目标；不遍历 entries，不覆盖所有 decoded allocations |
| Skia attached global glyph strikes | attached bytes/count、当前 byte/count limits | attached 集合的 purge 策略；不覆盖 detached、TLS、Typeface/所有字体 heap |
| Skia retained global resource records | retained bytes/count、当前 total/count thresholds、raw/effective single limit、discardable mode | retained records 的策略；不覆盖其他实例或淘汰后仍被外部持有的数据 |

组件状态 `0/1/2/3` 分别是 not_started/ready/busy/unavailable；仅对应 ready 的数值有效。未创建实例不代表已知零。组件锁各自独立，因此不是所有 heap 同一瞬间的原子快照。非 discardable 的有效单次分配限额为 raw 与 total 的较小值，raw=0 使用 total；discardable 的 raw=0 表示未显式上限，total=0 不表示零字节 heap 预算。软目标超出、缓存使用量降低、实际配置变化和最后 Host 关闭后缓存仍非零都可以是合法观察。

Acceptance App 在原 scenario 六个参数之外接受 `--cache-observation=1`。仅开启时，现有 sample 日志增加 `nativeSdk.namedCaches`，并在该次日志之后提交一次异步采集；下一 sample 可能收到该完成副本。所有 uint64 都以规范十进制字符串保存。冻结的公开 mixed/session 两种 8h policy 显式传入此开关，其他 scenario 默认不采集；原预算和负载参数不变，当前控制器接线仍不代表实际 8h 已运行。DTO 分析 `native-cache-validation.cjs` 仅检查数据：matched/partial/invalid 不升级 resourceGate，`passed` 和 `qualified` 始终 false。关闭后、pending/busy 重读保留原采样时刻且不能声称新任务完成。

公开资源校准入口为 `tools/Test-Acceptance.ps1 -ResourceCapture -CacheObservation`，可组合原 Architecture、RuntimeBinary 与显式 LLVM 参数。它仍执行原 10 次预热、100 次正式重载、每轮 5000 ms 静置和 20000 ms 步骤门限。此选项冻结新的 policy 字节、request 的 `namedCacheObservation` 和原 Owned context 第七个参数；旧 receipt 或只在 DTO 添加字段不能获得启用资格。未选择时原资源 policy 字节、六参数与结果字段保持兼容。

新有限 `configurationGate` 检查规范身份、配置一致性及 Skia retained resource 的实际清理后约束。非 discardable retained bytes 必须严格低于正 total 阈值；discardable retained count 必须严格低于正 count 阈值。相应阈值为零时 retained 集合应为空。总 retained bytes 不与单次分配 cap 比较，discardable total=0 不产生字节预算。Blink 和 attached glyph 的 usage/target 比较只报告 under/equal/over，超出软目标不单独失败。实际配置变化和每个原始点都保留。

完成副本的精确年龄使用 sample/capture QPC 和频率，最多 180000 ms；它不证明各组件的原子采样时刻。新 completion 必须具有递增 sample QPC、sampled 状态且没有 inflight/retired。资源校准要求基线 cycle10 和全部 cycle11–110 各有新鲜完整副本；缺失、partial、unsupported、stale、重复或退休副本保持未评估。已知当前 ABI 矛盾或 retained 违反为 failed，优先于其他缺失。最终显式 `captureGate` 合并原 OS `resourceGate` 与命名校准 `finiteGate`，不是新 heap 预算；failed 优先于未评估，未评估优先于通过。原数学 `resourceRuleObservation` 与 `resourceGate` 仍相等，显式校准未通过时公开入口返回非零。只有最后所有输入/pin 重检成功后，采样结论才出现在最终 capture。

当前版本已完成 DTO 的自身 ABI、规范 uint64 和已发布配置约束先于外部绑定检查：同点缺少 captureClock 不能掩盖已知违反，其他小时采样足量也不能抹去它。反之，合法 DTO 缺 subject/module/clock 本身只保持未评估，不冒称数据错误或新鲜采样资格。

8h 子门只对 eligible fresh complete 点给配置通过，非 eligible 点保留诊断并不算通过。每完整小时仍需至少 30 个不同完成副本，观察时间的相邻和小时边缘间隔最多 180000 ms；单个 partial 点不否定另外足量完整的覆盖，任何已知违反仍失败。`calibratedByteBudgets` 始终 null，`calibratedByteBudgetGate` 与整体 `cacheBudgetGate` 始终 not-evaluated。当前校准实际未运行；必须先采集当前 DLL 的原负载数据，再另行冻结经验字节预算及补齐未观察范围。

当前已运行 Node 数据/源码测试；新增 C++ layout/slot/binding/serializer 测试尚未编译。实际两架构 ABI、错误线程/尺寸、初始化前后、锁竞争和关闭竞态仍须由维护者执行；未产生 Native 缓存、长期运行或发布资格。

原 `soui_engine_host_capacity_tests` 的 `renderer-cache-abi` 独立用例复用固定 `--kernel`、`--kernel-sha`、`--resources`、fresh `--output`、`--case renderer-cache-abi` 参数和 30 秒预算。它声明实际加载表/preinit/拒绝 buffer 写入、真实 Host 采样、单任务在途和 queued-before-close 的晚发布拒绝验证，不要求所有组件都已创建。当前只新增验证源码，尚未执行；原三 Host case 与纯数据 case 不因此改成新的缓存行为证据。
