# 0084: TaskStore 私有请求字节配额

状态：Source 候选；编译、原生测试、SDK 接入和生产额度校准均未完成。

TaskStore 的 map、key 和 retained string 共享一个实际 allocation_owner。诊断计数可禁用、饱和或暂时不可读，不能用它们决定是否允许分配。新的构造参数是可选、不可修改的 requested-byte policy。nullopt 保持不设置额度；显式零拒绝任何正字节请求。它只限制生产 nested allocator 实际向底层申请的字节，公共副本、准备对象自身、STL 堆保留与其他 SDK owner 不在此范围。

每次 allocate 先检查 count × sizeof(T)，再由栈上 reservation 获取池中额度，然后调用已有故障 hook 和实际 std::allocator。异常析构 reservation 恰好返还尚未交付的额度；成功 commit 后额度属于实际 buffer，只有匹配 deallocate 完成物理释放后才返还。旧记录、准备候选、提交后待析构记录同时收费，任何 phase 切换都不返还额度。空 nullptr/0 cleanup 保留现有直接返回行为。聚合加法溢出或退款下溢使池失效并拒绝之后的请求，不能饱和成可用容量。诊断数据和 DTO 保持原语义。

池在 prefix/maps 之前构造，在全部 buffer 和独立诊断域之后析构；使用 Store 的现有锁，不增加线程、setter 或动态额度。额度拒绝是单独的 bad_alloc 派生异常，因此已有准备与 nonthrow swap 保持完整回退，ID/sequence、message/result 与 lookup 不被半发布。

回归使用真正生产 Store 与 nested allocator：disabled/scalar 两种模式、显式零、实际请求的边界、map/string 聚合、候选与旧数据重叠、底层逐次分配故障、六种状态变更回退、最终释放，以及诊断饱和独立于配额。夹具按本次实际请求计算额度，不把测试数字当成生产校准。mark_running 的相同 task-id 请求在既有固定额度下未必能形成新的额度峰值，其异常回退由配置额度的底层故障用例验证；不会声称已强制产生该方法的额度拒绝。测试还未运行。

本片没有给 task_service/json_bridge 开启额度。当前 task_service::finish 在 Store complete 前发布结果并释放 active，而 complete 可以抛出；服务接入需要两阶段终态发布及真实 owner 上预留、无分配的失败收尾，不能靠另一次有可能失败的分配重试解决。其他 service/shared pointee、Engine、permissions、renderer、完整堆范围与配置校准仍待验证。Store 子域和 full cacheBudget 都保持 not-evaluated，生产 quota/calibrationPin 为空，发布资格为 false。
