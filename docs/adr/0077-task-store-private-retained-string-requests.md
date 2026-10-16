# ADR-0077：TaskStore 私有 retained string 分配请求

状态：proposed，Source-only。releaseReady=false；完整 cacheBudgetGate=not-evaluated。预算、quota、配置与校准 pin 均为 null。本切片未编译或执行 C++、Native、SDK、VM、GUI。

## 冻结范围

在 ADR-0075 的三个私有 map 之外，跟踪 TaskStore 私有持有的字符串：map key/value、私有 snapshot 的五个 string 字段和 id_prefix。公开 task_snapshot、operation_result、std::string 参数/返回类型及 renderer168/model264/cache216 POD 保持原样。持久表示使用私有 basic_string 和 std::allocator 委托；成功 allocate(n) 才增加 n*sizeof(T)，真实 deallocate(p,n) 后才退还。这是 allocator 请求字节，包含 char buffer 的 terminator 请求，不是 capacity、malloc usable size、PrivateBytes 或全 SDK heap。

同一内联 allocation owner 给 map allocator 和 char allocator 提供身份。owner 保存 map、string、aggregate 三份计数；每次成功分配/真实释放同时更新对应 domain 和 aggregate。aggregate high-water 在事件时更新，不能相加两个 domain 的 high-water。SSO 的内联字段包含在持有它的 map node 请求里，不另计 char heap；Store 自身及其内联 prefix 仍属于外部 object-storage gap。copy/rebind 保留真实 owner 指针，is_always_equal=false；只允许同 owner swap，并用实际 STL noexcept 静态约束保证 commit 不抛。

候选 map 的 owned key/value/string copy 与旧 map 共存时都计入 live 请求，commit swap 不增加或退还请求；候选失败、回滚和 retiring 旧副本实际析构时退还。prepared_start 原有第一成员 Store 锁持续到 map/string 析构完成。owner 声明先于全部持有缓冲区，最终析构先释放缓冲区再退休账本。这里只观测总请求，不伪造 committed/prepared/retiring 的阶段分区或最终公开 disposal receipt。

lookup 使用仅在同步 find/at/erase 表达式中存活的 borrowed string_view key，不分配；每次持久 emplace 都显式构造 owned key。owned 视图按访问时的 buffer data/size 计算，不能缓存 SSO 地址。身份 hash/equality 比较完整字节序列，包括内嵌 NUL。查找仍使用 unordered_map，不改为线性扫描。

## 业务行为与失败

message/result 更新先完整构造私有候选缓冲区，再用同 owner 的无抛 swap 发布；分配异常不改变状态、progress、sequence、task ID、operation/scope lookup。后续实际析构才退还旧缓冲区。公开传入 std::string 的备用 capacity 不转移给私有不同 allocator：保存相同字节值，capacity 由实际私有 STL 决定。size/capacity 继续只是诊断，不能据此补充真实请求或保留原外来 reserve。无新容量/字节 quota，不清终态历史，不改取消、续期、权限、关闭、重试或时钟政策。

准备中的公开 receipt snapshot、evicted_id、入参、任务 ID 格式化临时字符串、query/start 返回副本尚未覆盖；其他 service/job/subscription/pointee owner、allocator 内部 reserve/fragmentation、kernel cache 都未覆盖。失败的公开边界分配也不冒充私有 allocator failed count。

## Phase 1 读取边界

旧 read_storage_metrics 与 task_store_storage schema/字段/coverage=3/gaps=63 保持 ADR-0075 的 map 请求和 committed string 诊断语义。新增不同名 read_retained_string_storage(metadata, string_requests, aggregate_requests)，在同一个原 Store try_lock 和 epoch/sample envelope 下读取三 domain；新输出使用已有 allocation_requests 类型，不追加旧 DTO 或 ABI 字段。metadata 的 masks 仍属于旧 DTO 范围；新字符串输出的 exact flags 只表示此次私有 allocator 请求。

disabled/busy 不留下看似精确的零；累计饱和保持 sticky 标记，current 溢出/下溢保持 UINT64_MAX/inexact，检测不一致返回 unavailable。getter 不分配、复制业务字符串或修改账本。当前 service/facade/serializer/collector 尚未接入新 getter，所以它单独不能作为实际 cache captures 的新观测证明；Phase 2 在整合后的 Source 上另做 versioned DTO 和现有公共采集路径。

私有 TaskStore C++ object layout 与 STL 请求 footprint 会改变，旧 POD 尺寸未动不代表 C++ 二进制 ABI 不变。SDK 与所有消费者必须整体重编，之后通过公共 Source/compiler/ABI/Owned 证明与真实重载/长期校准；不能复用先前二进制或基线。

## 验证与备选

保留已有正常语义和 fault target，用真实 TaskStore 实现新增长 key/scope/prefix/message、终态留存、双副本 aggregate 峰值、每个真实 allocator allocation 的 bad_alloc 回滚、message/result 候选失败、同值短 string/外来大 capacity、无分配 lookup、disabled/busy、实际最终退还、饱和/不一致断言。当前只执行源码/pure 检查；这些 C++ 测试未编译或运行。旧 serializer 继续显式 stringAllocatorExact=false/fullBudget NE，Phase 1 不扩大其声明。

不采用全局 operator new；不把公开 string 改为 custom allocator；不从 capacity 估算分配；不把不同 owner 声明为等价；不新增观察线程或 purge。更广 owner 与硬容量限制是后续独立切片。
