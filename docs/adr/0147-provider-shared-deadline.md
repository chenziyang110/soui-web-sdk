# ADR-0147：显式共享预算的 provider 截止与有限续接

日期：2026-10-30。状态：proposed；源码与限定回归通过，默认共享预算仍为 0，未构成性能或发布资格。

## 背景

Engine 的显式共享预算已限制 native/input/FIFO 的合作边界，但无参 owner opportunity 未收到该截止。provider 同时执行 scheduler 与 deferred-close/retry；只截断 scheduler 会遗漏关闭扫描，并可能在 Engine 进入无限等待后失去续接机会。

## 决定

- 内部安装事务同时替换旧无参 callback 与有参 callback。原公开 setter 同时清除有参 callback；捕获在锁外退休，卸载前清除两者。默认 0 和旧 provider 保留原调用。
- 私有 v1 ABI 只追加 capability `0x400` 和 `serviceOwnerOpportunityUntil(deadlineQpc, qpcFrequency, workRemaining)`；先检查 version、structSize、capability、指针，保留原字段偏移、调用约定与导出。
- SDK 在边界采 QPC，再采 steady clock，只把剩余 duration 向下换算并检查溢出，加到先采 QPC。两种 epoch 不直接比较；采样失败只允许在执行前回退一次。
- 同一 QPC 截止传入 SharedTimer 的正常及恢复入口、scheduler 和 close/retry。频率为零表示内部无截止，截止数值零不表示禁用。QPC 只限制预算，WTF 时间仍判断任务到期。
- 截止已过期仍保留最低进展：已 ready callback 不被 admission 饿死；只有 pending admission 时有界接纳；关闭扫描独立保留至少一次 live-view 访问。不可抢占正在执行的 callback、析构、绘制或关闭。
- close/retry 保留当前最多 32 次的有限 sweep 剩余数与 fresh-lookup cursor。只在 ready/admission 或本轮 sweep 尚未结束时报告立即剩余工作，复用 Engine wake event。future timer、等待 reader、纯 idle 不自激唤醒。
- 显式 shutdown 排空、代际、资源 fence、owner/reentry gate 及默认策略保持。部分工作后失败不重新运行 legacy provider。

## 影响、恢复与覆盖边界

对象为进程 Engine、provider owner、shared timer、scheduler admission/ready 队列及 live-view sweep；adapter 先清 callback 再 finalize，防止悬空函数指针。旧 DLL/短表/缺 capability/空尾指针走旧入口。回退本 ADR 对应独立提交恢复 provider 调用，保留 ADR-0145/0146 的时钟与绝对任务截止。

不选择新的默认预算，不改变公开 WKE ABI 或页面逻辑，不引入依赖。拒绝新增轮询线程、永久 timer、普通 FIFO 自补充链，避免 idle 忙转。完整 kernel、匹配消费者、物理输入、实际载荷尺寸与性能另验；小夹具和源码投影不能替代这些证明。

## 验证

已执行：x86/x64 LLVM22/MSVC142 的实际 Engine/mailbox/clock/ABI/fallback 各 1/1；scheduler worker 各 43/43、SharedTimer continuation 各 3/3；实际 InputUiApi/CWebView 方法投影各 9/9、166 断言。真实 Engine 覆盖默认/有限预算、单次分派、替换清除与锁外退休，包括四种重入替换/清除后读取原 capture 的场景；正常 turn 只交换 callable，无逐轮复制/分配。

x64 SDK 静态库与 input-mailbox 实际增量编译 28.252 秒、CTest 232ms，143 输入前后固定，进程自然退出。SOURCE 门禁 80/80，53 个七类 recipe 输出和 17 个 worker 输出受严格派生规则约束；SOURCE 是 authored graph 检查，不是完整编译资格。0028 八文件 raw-byte 补丁正反向重放及当前树反向非写入检查通过。

首轮 worker shutdown 测试在 capture 应退休前检查，修正断言时点后通过，生产未改；两次 SDK 编译前 CMake 失败保留。集成核对旧 dragdata 的两个片段因 ADR0146 移动 28B、ctor_shell 六片段因本批前部插入移动 682B，原片段均与 fixture 完全相同且唯一，仅修全文件 pin/offset，HEX 门禁通过。未删除或放宽门禁。

证据身份见 `third_party/manifests/miniblink-provider-deadline-20261030.json`。用户要求减少耗时测试，未启动完整内核重建或重复既有通过项；完整生产 TU、匹配消费者/实际加载、物理输入、性能及载荷尺寸均待验证。
