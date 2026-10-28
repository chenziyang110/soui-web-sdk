# 调度索引回归源码

默认种子 20261026 的 100,000 次插入/取消/re-arm 序列对照旧策略的 priority/deadline/50 ms 老化键；等键身份另验证新的 queue-entry 顺序，绝不声称与旧 heap encounter identity 完全相同。任意整数 priority 迫使 AVL 旋转/移除节点；future/ready 摘要与直接 oracle 对照，退役后最多保留四个标准优先级空节点。

当前未编译、未执行。专人入口：`cmake -S tests/native/scheduler_index -B out/scheduler-index`，构建后执行 `ctest --test-dir out/scheduler-index --output-on-failure`；实际 owner、self-delete、引用计数、原生压力和关闭由 worker_lifetime/scheduler_continuation 源码集合覆盖。性能需另外记录不同 total/ready/priority-class 数的成本和新增索引字节，不能由算法复杂度冒充实测结果。
