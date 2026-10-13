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

当前已运行 Node 数据/源码测试；新增 C++ layout/slot/binding/serializer 测试尚未编译。实际两架构 ABI、错误线程/尺寸、初始化前后、锁竞争和关闭竞态仍须由维护者执行；未产生 Native 缓存、长期运行或发布资格。

原 `soui_engine_host_capacity_tests` 的 `renderer-cache-abi` 独立用例复用固定 `--kernel`、`--kernel-sha`、`--resources`、fresh `--output`、`--case renderer-cache-abi` 参数和 30 秒预算。它声明实际加载表/preinit/拒绝 buffer 写入、真实 Host 采样、单任务在途和 queued-before-close 的晚发布拒绝验证，不要求所有组件都已创建。当前只新增验证源码，尚未执行；原三 Host case 与纯数据 case 不因此改成新的缓存行为证据。
