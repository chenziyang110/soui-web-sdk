# ADR-0078：独立采集 TaskStore 私有 allocator 请求

状态：proposed，Source-only。releaseReady=false；完整 cacheBudgetGate=not-evaluated，configuration/budgets/quota/calibrationPin=null。C++、SDK、Native、compiler、VM、GUI 均未执行。

## 决定与范围

在 ADR-0077 的真实 map/私有字符串/aggregate 请求账本之上，新增独立 schemaVersion=1 的 task_store_private_storage DTO，以及 TaskStore、task_service、json_bridge 的不同名字 getter。复用已有 allocation_requests/sample 类型，不追加旧 task_store_storage、runtime、renderer168/model264/cache216 的字段。旧 getter、mask=3/gaps=63、taskStoreStorage JSON、stringAllocatorExact=false 原样保留。两个相邻记录有各自 observation，不拼接为一个原子快照。

新 DTO 一次调用 ADR-0077 的三参数 getter：三个请求域和三个 entries 来自同一个原 Store try_lock、epoch、begin/end envelope，getter 仅复制 POD。coverage=7 只标识 map requests、私有 owned string requests、事件更新的 aggregate requests 三域；它不声明任何容量/heap/阶段边界。busy/disabled 不提供 exact 零，饱和与 accounting mismatch 保留 unknown/UINT64_MAX 或 unavailable；service 继续原 service→Store try_lock 顺序，facade 不引入新线程或业务动作。

新 gap enum 固定为五个命名位，总 mask=31：public_temporary_copies_gap 包括公开入参、prepared receipt/evicted_id、格式化 ID 和 query/start 返回副本；phase_partition_gap 表示无法独立区分 committed/prepared/retiring；other_sdk_owners_gap 包括 service/job/result/subscription/handler/shared pointee 等其他 owner；allocator_heap_external_gap 包括 object 外部存储、allocator reserve/fragmentation 与 whole heap/kernel cache；final_disposal_receipt_gap 表示真实析构后没有可读公开 receipt。私有 candidate/retiring buffer 的真实 allocate/free 已进入请求域，不能与未覆盖的 public prepared copies 混称。

## 既有 acceptance 接线

仅现有 --cache-observation=1/native owner capture 分支新增 taskStorePrivateStorage 与 getter 实际返回 status。序列化明确 scope=task-store-private-allocator-requests，每个 U64 用规范十进制字符串；精确性取自各请求字段，标注 public copies/phase/whole heap/final receipt 未观测，不输出猜测数字预算。旧 taskStoreStorage 分支字段完全不变。host close 成功后、owner 真析构前的既有记录也走同一 native_owner_metrics 路径；捕获一次后保留原 last_retired_owner，不重新采样改写历史。

现有 long-run-owners reader 的 checkView 读取指定 bridge/adapter/lifetime/epoch 字段，不拒绝 view 额外 keys；它仍固定比较退休 view 整对象。resource-validation 沿事件日志/raw binding 处理旧资源/named cache/model，没有评估旧或新的 TaskStore storage 字段。相邻新字段因此可保留，但这些读者尚不能把它标为已通过的独立 accounting/calibration；本切片不修改 readers/runners，也不宣称实际 capture 已执行。

## 兼容性、迁移与验证

默认生产 diagnostics disabled，输出没有新字段；没有新 Web method、权限、quota、purge、取消/续期/关闭行为。SDK 和所有消费者仍须针对新的 TaskStore object layout 整体重编；旧 POD 源码不变不等于已完成 C++ ABI/实际工具链证明。Phase 1 封存输入不修改，在 1384 的 fresh Source 中精准导入其三个 C++ members 与 ADR0077，README 内容合并保留 ADR0076/0077/0078。

真实 C++ 回归源码覆盖 Store/domain 同 envelope、旧与新 getter 范围、service/facade 默认 disabled 与真实 retained payload、独立 DTO/serializer exact U64/null policy、busy/saturation/unavailable。静态 Source 检查覆盖 opt-in 唯一路径、正常/host-closed 相同入口、旧 serializer/DTO/ABI headers 不变，以及既有 reader 不拒绝新增字段。当前只有 Source/文件检查，所有 C++ 断言尚未编译或执行。后续由普通公共 Source/compiler/ABI/Owned 链完成整 SDK/消费者重编及真实 capture，再推进其他 owner、硬容量限制和 100 reload/8h 校准。

不把新字段塞入旧 DTO，不把 capacity 标为请求，不对不同 observation 拼接 aggregate，不从覆盖位推导全部缓存上限或解除完整预算 NE。
