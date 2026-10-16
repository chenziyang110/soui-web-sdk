# ADR-0070：有限命名缓存观测

状态：proposed

日期：2026-10-13

现有 renderer 168/264 字节 ABI 不观察 Blink/Skia 缓存。现成缓存 getter 会创建单例，遍历统计还会改变采样成本，因此不能作为 UI 只读诊断。

新增独立可选 cache ABI 与 SDK 完成副本 DTO。显式 request 在已有 Engine ordinary lane 提交一个在途任务；UI getter 仅 try-lock 读取已完成副本，不 post、pump、wait、初始化或调用缓存。任务在实际 Engine owner 读取 Blink 现存 MemoryCache 的 O(1) live/dead 字节和当前软 prune 配置；Skia 只对已发布实例 try-lock，读取 attached global glyph strikes 与 retained global resource records 的标量及当前配置。每个组件独立锁定，不宣称跨组件原子采样。

Blink capacity、glyph byte/count limit 与 resource purge 配置是实际策略阈值，不能泛称 heap 硬上限。resource discardable 模式无 byte budget，raw single-allocation limit 0 表示默认；非 discardable 的有效限额为当前 total 与 raw 的较小值（raw 0 则为 total），discardable 的 raw 0 表示未显式限额。当前策略本身不改变。Blink entries 未覆盖；detached glyph、TLS、其他 Skia 实例、淘汰后外部持有对象、所有字体/解码 heap、V8 compile cache 和 allocator overhead 均不在范围。

无实例、锁竞争、错误 owner、未知身份/时钟、部分组件或异常各自保留非 ready 状态，数值仅在对应组件 ready 时有效。SDK 绑定加载模块位置与实际 PID/creation/epoch/owner/QPC；路径匹配不是二进制载荷证明。关闭开始使在途 generation 失效，晚完成不得发布；保留最后完成副本和原采样时间，进程缓存不要求最后 Host 关闭归零。新观测不改变 shutdown/prepare/finalize、旧 renderer ABI、队列预算或任何资源合格门。

源码规则验证和 Node 数据测试可执行；Native 编译、接口探针、缓存行为与长期运行验收由 Root 后续执行，当前均未验证。本修改在独立 worktree，旧发布 Source 与历史实际记录不改。

调用者使用 `request_miniblink_cache_sample()` 显式提交，之后用 `read_miniblink_cache_metrics()` 读取完成副本；不保证同一 UI 事件就有新样本。独立 Acceptance App 可在原六个 scenario 参数之外添加 `--cache-observation=1`，每个已有 sample 写完日志后提交一次，下一 sample 可能得到该结果，普通运行默认不开启。最后关闭只保留原 sampleQpc；必须与 captureQPC 比较，不能把副本重新标成新采样。公开八小时入口按 [ADR-0071](0071-long-run-cache-sampling.md) 在固定策略中明确请求此观测，原时间和场景负载保持不变。

新增 C++ 数据测试使用生产 slot/binding/serializer，检查单任务在途、拒绝后重试、旧 token/关闭后不能发布、身份失配锁定、软策略变化、精确整数与退休副本；声明 x86/x64 216 字节新布局以及原 168/264 字节布局。Node 的 DTO/源码测试只执行数据验证和 selected Source 检查。原生 GetApi/错误尺寸/错误线程/初始化前后、真实缓存实例/锁竞争、异步关闭竞态及两架构实际编译仍需单独执行。若新 DLL 缺少 API，optional getter 为 unavailable，旧 renderer/业务路线继续原样。

既有 `soui_engine_host_capacity_tests` 新增独立 `renderer-cache-abi` case，保留原三 Host case、纯消费者 case 及 30 秒预算。它使用实际固定 DLL/hash/resources、真实 Host 与原有限 owner service：原生版本/尺寸（拒绝且 buffer 原字节不变）、preinit、wrong UI owner、异步完成 PID/creation/owner/epoch/QPC、重复纯 getter、不强求每个组件已创建。关闭前销毁 Host 并 join Bridge 后，用原 Engine hold 暂停真实 owner，将一个采样排在后面；启动真实 shutdown 后放行，从实际完成与退休副本不变核对晚发布拒绝。这个受控 queued-before-close 路径不证明所有并发时序；组件锁竞争、DLL 缺接口的真实替代件和全部关闭竞态仍未执行/未覆盖，不借纯 slot fixture 外推。
