# ADR-0122：输入到可见反馈的分阶段优化契约

- 日期：2026-10-26。
- 状态：设计决定；各工作包实现、静态审查和未执行验证见实施台账。
- 源码基线：`44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`（Draft PR110）。PR104、105、107、108、109、110 当前均未合并；本系列明确堆叠于 PR110，不改变其既有验证结论。

## 背景

维护者要求按输入接纳、引擎调度、任务截止时间、布局提交、资源完成和呈现的完整链路持续实施。PR110 只是一项未完成编译和实测的源码候选。其同线程 createTime 修改未改变最终 deadline，不能作为性能收益。legacy UI paint 节流重投风险与默认 DIRECT_HWND/independent 路径必须分别处理。

## 决定与实施顺序

1. W00 先更正计时叙述并删除无行为作用的局部计算；审查 pending/deferred/closed、消息票号、投递失败、重入和重建。stop/recreate 不得复用旧 HWND、timer 状态或消息票号。scheduler oracle 保留已有优先级、deadline、50ms 老化与当前 encounter-order 等键行为；不宣称原实现严格 FIFO。
2. legacy/non-independent dirty 等待使用一个可取消 deadline 任务，期间合并 dirty。未到 deadline 不立即重投；投递拒绝、任务取消或分配失败不得清掉 dirty。销毁及尺寸 generation 拒绝旧任务；相关引用在 owner 回调完成前保持有效。默认 DIRECT_HWND/independent 路由保持独立验证。
3. W01 冻结版本化数值事件、会话身份、QPC 频率、input/task/frame/generation 关联和终态。详细记录默认关闭，每线程预分配有界 ring；热路径不格式化、不分配、不打开文件、不持文件锁、不向引擎投递落盘任务。scalar 模式使用固定计数和直方图，溢出使完整因果轮次无效。vendor 不反向包含 SDK 头或依赖 SDK 对象；跨模块入口必须检查版本、尺寸、代际与存活。
4. A 线串行完成 W02 输入接纳/恢复、W03 deadline 唤醒、W04 索引调度，再做 W09 宿主成本诊断和 W10 WebView2 专项。关键输入使用独立有界保障，不挪用 control/renewal/cleanup。拒绝必须类型化并由宿主处理；无法继续保持输入状态时明确取消和恢复，不能静默记为成功。保持多 Host 进展与因果屏障。
5. B 线串行完成 W05 帧 deadline/分数相位、W06 资源通知、W07 mirror/buffer lease，再做 W08 有界 raster 候选和 W09 渲染诊断。复用已有 pacer、dirty copy、资源完成和 scene 单槽；注册等待后重查条件。保留 action 区间、ready-through、scroll coverage/fence/ACK。锁外绘制必须使用有生命周期的 lease，不能继续使用已解锁裸指针。
6. 任务、通知、缓存、记录和 buffer 均有项数/字节上限。promotion、取消和接纳本身受协作预算约束；交互优先不得使后台永久饥饿。普通 DOM move 不因性能目标额外丢弃；wheel 合并与 fallback 必须保持目标、epoch 和位移守恒。
7. future 不早跑，ready 合并唤醒，idle 取消额外周期轮询；native-pressure 有有界恢复机会。使用单调内部时钟，保留 Win7 的正确能力探测与 fallback，不改变 JS Date 或永久全局提频。分数帧相位在刷新率/DPI/显示器变化时重建，迟帧跳过，恢复不重放 pulse。
8. W08/W09/W10/W11 中需要 profile 才能决定采用的部分先交诊断、设计及必要的默认关闭有界候选。缺数据标“待数据决定采用”，属于未关闭工作。GPU 单独候选、独立开关与软件回退，不代替软件主线，也不默认启用。

## 备选方案与取舍

不采用把 timer 改成 0/1ms 的周期轮询、Sleep(0)、实时优先级、无界 drain、丢输入、跳过 fence 或强开 GPU。暂不以无竞争数据的 lock-free 或大规模 layout 重写代替可证明的有界结构。协作预算不能抢占任意 JS/客户端回调，因此不能承诺所有回调都满足固定延迟。

## 兼容、迁移和回退

保留 Windows/MSVC/C++ 基线、旧嵌入者回退、Chrome49、既有授权/导航撤销及线程归属。内部 C++ 布局变化必须重编匹配内核、SDK 和消费者；固定预编译 DLL/LIB 未替换，不能声称包含本系列实现。旧 serial-ACK/DPI/IME 协议不放宽，新同产品双提交和 burst 协议单独版本化。按工作包提交和 Draft PR 回退生产修改及对应测试/来源映射；不混用旧对象与新头文件。

## 验证与事实边界

维护者明确要求本实施阶段只交生产代码、必要回归用例源码、静态审查、来源、文档和 Draft PR。**本 agent 不编译、不执行测试、不启动 VM 或性能工具**。编译、正确性、真实输入/桌面和物理显示测量由专人按一次性交接清单完成。T7 桌面 ACK 不冒充 T8 发光时间；未关联 microtask 不伪造 T3b；所有 uint64 量测字段以十进制字符串导出。每包状态独立，PR 创建不代表性能通过，生产资格保持原结论。
