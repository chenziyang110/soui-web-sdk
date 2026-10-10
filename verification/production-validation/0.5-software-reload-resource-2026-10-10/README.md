# 0.5 软件百轮重载资源记录（2026-10-10）

本轮结论为 **x64 PASS、x86 FAIL、整轮退出 1**。两个实际主体都自然退出 0/0，完整 Job 均只有该主体、无强制清理；操作循环完成不等于资源门槛通过。整轮退出码由 Root 的 Native session 48167 关闭结果报告，本目录没有伪造控制器 Owned 回执。

| 架构 | 实际 PID / 创建 FILETIME | 私有内存尾部增长 | 句柄基线 → 尾部中位数 | 原资源结论 |
| --- | --- | ---: | ---: | --- |
| x64 | 68224 / 134361049789856195 | 4,458,496 B | 300 → 300（+0） | PASS |
| x86 | 83776 / 134361055811640115 | 3,928,064 B | 315 → 318（+3） | FAIL |

x86 的唯一失败项是句柄尾部增长 +3 超过原阈值 0。两架构句柄 Theil–Sen 斜率均为 0；这不能取消尾部门槛，也不能据此证明或排除某类对象泄漏。其他四项被门槛约束的指标及逐轮窗口类检查均通过，工作集仅描述。

本轮严格保留原 reload100 / warmup10 / settle5000 / step timeout20000。各架构有 348 条原始事件、116 个步骤开始/完成对、114 个样本与 100 个测量轮次；实际最短稳定等待为 x64 5103 ms、x86 5117 ms。原监督器预算为 3110000 ms、drain2000，原逐步监视与资源 reader 未替换。第 10 轮是基线，最后 10 个测量样本取中位数；私有内存额度是 max(16 MiB, ceil(基线/10))，句柄、线程、GDI、USER 的尾部增长及趋势额度仍为 0。每指标 4950 对斜率由两位复核者以原始样本独立重算。

实际输入是 source ad8d40a0e73a5262a0c741c2c3148f22d5f9eb6fce36501eaf6ab6fd17438f30 的当前正常 SDK 和两套 fresh public Web consumer。1095 源文件按原 normalization 在运行前后匹配；每套 75 个冻结 runtime 文件匹配。新准备的 host x64 OwnedProcessRunner 为 150016 B / db99e2dcdf203f1ebcb4c9892061bf2c8e9893186f32e7932176b745825665ce。两个新 source-navigation.log 单独保存在 diagnostics，不加入原 payload 身份。

附件路径与来源见 [copy-index.json](copy-index.json)，全部目录成员按真正 StringComparer.Ordinal 排序，除 manifest 自身外的原字节 SHA256 见 [manifest.json](manifest.json)。原 inputs/bindings/result/review 中绝对路径保持原文，relative 附件映射仅用于离线审查。局部 .gitattributes 使用已通过消费者归档的规则，Git 保留原字节、换行及 ident 内容。复制前后来源哈希一致；所有源代码与控制脚本复本为 inert/raw，只供审查，不构成可执行交付。

本目录保留本轮全部成功与失败文本记录，不覆盖历史 315e 双架构资源失败。有限的 100+10 软件单进程资源单项不提升 SDK 私有缓存/队列、1000 轮、8 小时、目标 OS/PE、完整工具链、M0 或正式发布资格；releaseReady 与 formalPassed 均为 false。后续工作区变化不能把原冻结 pin 重标为当前匹配。

下一步最小诊断是复用已有 V3 PSS 自身句柄观察协议，创建新的 x86 诊断 consumer，绑定本轮同一正常安装 SDK、候选 kernel 和真实 PID/FILETIME。原失败原样封存；诊断与原无观察器 gate 分开标识，保留 100+10、5000 ms 和原监督预算。每个原样本先 flush，再采集对齐的 PSS sidecar；只看自身 PID、记录采集前后计数、QPC 与捕获/遍历/释放状态，保持既有 4096 行上限和 250 ms 事后限制，不延长门槛或施加 GC。

可证伪假设是“多出的 3 个句柄集中在一类且在稳定等待后持续存在”。若相应类型尾部计数不增，或探测前后计数不稳定，则不能据此归因；若计数一致且集中，再沿真实持有/释放路径查因。PSS 类型 6 为 semaphore、0 为 unknown；opaque handle 值不能证明同一对象或分配身份，类型中位数也不能简单相加。PSS 不可用（含旧 OS）应记 unavailable，不能冒充目标系统资格。只有修正真实原因后，新的正常无 PSS 原 gate 运行才能评估是否消除 +3；本目录失败始终保留。
