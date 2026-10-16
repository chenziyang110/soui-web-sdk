# ADR-0112：有界的进程地址空间观察

日期：2026-10-16。状态：实现已整合；双架构原生扫描及 51 项读取回归通过，最终候选与实际负载校准待验。

目前命名缓存数据只覆盖 Blink、附着 glyph 和 Skia 驻留记录。它们不能解释其他分配器、脱离缓存的对象和映射内存，不能相加当作进程总量。已有 PrivateUsage 也不能与 VirtualQuery 的 MEM_PRIVATE 字节简单等同；写时复制页面仍可能报告 MEM_MAPPED 或 MEM_IMAGE。

在显式 `--cache-observation=1` 的验收消费者中增加只读地址空间遍历。采用当前进程的 GetSystemInfo 地址范围，以 VirtualQuery 读取区域属性；不访问页面、不锁页、不压缩工作集、不触发 GC，也不创建后台线程。每次最多 32768 次查询，QPC 观察期限 50 ms，API 返回后重新检查期限。它不能抢占被操作系统延迟的单个调用；超过期限如实记录未完成。

分别记录实际观察到的 committed private、mapped、image 字节及区域数，另保留 reserved/free 与覆盖范围。未知状态/类型、零长度、不包含查询地址的区域、整数溢出、查询失败、时钟失败、区域或时间预算耗尽均不能输出完整状态。计数是顺序观察，不是原子快照：其他线程可能并发分配/释放。共享页按本进程映射范围计数，不声称唯一物理占用、页面所有权或分配调用账本。GPU、驱动、其他进程和内核资源仍不在这个观察范围内。

本切片只补采样及独立标量读取，不改变原有容量、100 次重载、八小时资源趋势、时钟与自然关闭要求。读取器校验原生进程/线程与外部生命周期身份、架构、单调时钟、固定采样上限、区域计数和完整范围精确相等；保留缺失、部分和非法样本，不将其当作零。仅在资源读取器核对完整原始输入后添加 `addressSpaceObservation` 描述数据，原有门槛保持不变。后续必须使用实际新日志校准并冻结预算；不能直接把 `cacheBudget` 改成通过，不能用本扫描代替尚未解释的对象增长。

兼容范围沿用 Windows 7 SP1 及当前 x86/x64 构建，API 来自 Kernel32，不引入新系统 DLL 或最低版本。独立测试覆盖真实私有/共享映射和实际全范围扫描，以及失败、越界、时钟和预算边界；组合后重新编译实际验收消费者。

依据：[VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery)、[MEMORY_BASIC_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-memory_basic_information)、[SYSTEM_INFO](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-system_info)。
