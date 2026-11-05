# ADR-0163：合成唤醒提前接纳的受控实验

- 状态：提议；仅允许外部原型和有界源码夹具，尚未采用生产实现或改变默认值。
- 日期：2026-11-05。
- 所属任务：F03。依赖既有帧相位、资源 fence、paint lifetime 和任务退休契约。

## 背景与证据

`LayerTreeHost::queueDrawFrameWake` 在已有 `drawQueuedCount` 时直接返回；`drawFrameInCompositeThread` 的 `alreadyPaced` 判断则在旧任务真正开始后才执行。因此新主帧已经满足 deadline 时，其即时请求仍可能等到先前 delayed wake。

当前 preview.4 的一个 full64 诊断中，42个软件帧的 processed coverage→合成入口 P95 为13.3021ms，15个实际执行任务在覆盖记录之前已发布；没有 due/delay 字段，不能据此断言这些任务就是 delayed draw，或确认真实延迟根因。原始记录不变，详见追加性能报告和 `PR129-performance-20261104/repair-r16` 的独立分析。

独立外部源码夹具提取当前 queue、DrawFrameTask 和真实 pacing 前缀，使用明确的虚拟时钟/调度器及 fake raster tail。已有 delayed wake 时，虚拟1.004秒的新就绪帧到1.016秒才执行；无旧任务的控制在1.004秒执行；未 paced 的控制仍遵守限帧。LLVM22/MSVC142/19041 的唯一 CPU 运行及原生 Job 正常收口，不能称 v143 内核资格、真实12ms收益或现有 full64 长尾的因果证明。原配置/VCTIP、脚本准备和日志解码失败保留，未用重编覆盖它们。

## 实验决定

先验证一个固定容量的提前接纳原型：保留原 delayed wake 的失败回退，至多保留两个可调度 receipt 槽；只有可见、未关闭、unified 且新 main frame 已满足原 deadline 的即时请求才有资格竞争一次普通优先级唤醒。先成功 claim 者消费同一逻辑 intent，另一个逻辑失效，但必须等其真实 run/dispose 才退休自身义务。不携带或合并输入载荷，不改变资源/场景完整性和帧覆盖语义。

这不是“最多两个 C++ Task 对象”的承诺。callback 尾部可能仍持有独立 finalizing/finish 义务；若它释放可调度槽后接纳下一项，必须单独证明该有界尾部，不能把尚未完成的旧义务从计数中抹掉。WebThread 已完成对象的延后销毁另循原退休契约，不能靠清槽声称已销毁。

编码前及采用前必须分别关闭以下边界：

1. pending intent 保存原绝对 due。满槽或回调等待后，只计算 `max(0,D-now)`，不得从当前时刻重新增加旧 delay；后来的普通请求也不能延后已发布的更早 intent。
2. queued、slot 和 finish 的退休点分开。中央 Task run-scope RAII 保证正常、异常、claim失败的 exact-once 义务；finish 在最后一次 Host/queue访问后退休。析构只退本 ticket，不调用 Host、不递归 post，不影响较新 ticket。
3. stale 释放后最后一个 pending 必须有可证明的 owner 路径。正常 run 尾部可在保留生命周期证明时最多服务一次；取消/析构只保留 intent/retry 状态，不能锁反转或静默依赖不存在的新请求。
4. 保持 composite mutex→retry mutex 次序；不得持 retry mutex post或销毁payload。原型优先保留现有composite锁跨分配/post，仅在post前释放retry锁，以继续保护 `m_compositeThread`。若也在post前释放composite锁，必须另持有效owner/thread lease直到post消费完成，不能复制raw指针后解锁；同步拒绝时Task义务归零也不代表post已返回。关闭先禁止reserve/claim，再沿原composite join与任务清理排空；隐藏/恢复不继承旧帧的可见终态。
5. 两槽已满、分配失败、同步/异步 post取消、序号耗尽时不得创建第三个有效 timer，也不能先废弃唯一有效的旧 wake。所谓尾部上限须由实际义务计数而非放宽断言证明。

上述边界仍未完整证明时，原型保持外部实验，不导入默认 DLL。

## 备选方案

- 保持现状：作为每轮反例/control及运行对照，生产当前默认继续保留。
- 清零queued或替换旧ticket后再post：拒绝；旧任务仍由调度器拥有，可能破坏计数、回退及关闭。
- 每个即时请求再补任务：拒绝；不可取消的旧 timers可能累积。
- scheduler-owned 单任务提前调期：语义上能避免额外旧 timer，但当前 `void postTask` 无调期/取消句柄。若固定容量原型不能完整证明进度/生命周期，转为单独设计该私有调度器能力；不以增加无限槽位代替。

## 兼容性、迁移与回退

本提议不改公共 C++/TypeScript、WKE/provider ABI、线程归属、输入FIFO、main rate=0或已生效帧率语义，不绕过GDI/lease fence。若后续形成候选，先作为身份独立的实验内核，保持原发布版与preview.4载荷不可变；生产采用须依版本规则、双架构构建/相关回归、匹配消费者及真实A/B另行接纳。回退恢复原单wake路径，并部署完整匹配载荷，不单独覆盖DLL。

## 验证与采用门槛

先做外部固定容量policy/源夹具：primary先claim、promotion先claim、满槽连续请求、原绝对due、OOM、post取消、stale run/dispose、回调中再request、异常、隐藏/恢复、关闭后迟到事件、序号耗尽。检查计数非负、最终0、首个有效wake不丢失、无新增循环/无界timer、pending在owner上最终可达；不得删除失败路径或放宽原关闭期限。

只有独立审查通过后才允许修改vendor。随后对受影响闭包执行两架构构建和相关消费者，更新来源/补丁/散列；用相同消费者和负载冻结基线噪声后验证软件提交尾延迟、帧时间、CPU/内存及关闭回归。收益不确定即保留原默认，不以源码夹具或单轮短诊断提升 `performanceVerified` 或 `releaseReady`。
