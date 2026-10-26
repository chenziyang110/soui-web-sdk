# ADR-0125：有界的输入、任务与可见帧关联记录

- 日期：2026-10-26。
- 状态：实施决定；代码、回归源码及静态审查交付，编译和执行未运行。
- 上位契约：ADR-0122；源码基线 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`。

## 背景

现有 input_packet、scalar diagnostics、SourceFrameTrace 与 benchmark serial-ACK 各有明确用途，但不能提供完整逐输入、逐任务、逐帧的因果链。可选 Bridge/interaction 实验日志启用时逐事件打开文件；默认关闭的这些代码不是已经证实的主要卡顿源。新量测必须先证明自己的扰动预算，不能改变输入或呈现语义。

## 决定

1. SDK 和 vendor 分别持有独立 recorder，使用相同 v1 数值 POD 和精确会话身份；vendor 不包含 SDK 头，不持有 SDK callback、对象或 sink 指针。独立导出以 run_id、进程 PID/创建 FILETIME、QPC frequency 精确连接。run_id 由监督本轮的启动方提供，不从 PID 推导。
2. 注册只在实际 producer 线程的冷启动入口执行，最多 32 个注册流，每流详细事件容量 1024。详细模式默认关闭；scalar 模式不分配事件数组。注册失败和未注册写入显式计数，record 不懒注册。每个流仅一个实际线程写，一个 drain consumer 读。
3. 热路径只做固定 POD 校验、单 producer 计数/直方图、QPC 采样及有界 ring 发布；无字符串、文件操作、互斥锁、内存分配、等待或引擎落盘任务。drain 的项数由调用方预算限制；不创建后台线程或周期 timer。
4. identity/configure 在首次注册前冻结。本轮一个进程对应一个 trace session；重复运行使用新进程。stop 非阻塞拒绝新记录；producer handle 继续持有 storage，全部释放且 ring 排空前不能写 complete end。销毁 recorder 不能释放仍有 producer handle 使用的 ring。
5. 每事件包含 QPC、thread、view/document/geometry generation、input sequence 区间、task/frame 和 action 区间。零表示未知关联；不凭时间接近制造关联。一个 coverage 事件只声明一个连续区间，有洞的集合必须分段。frame 可以覆盖多个输入，输入可以关联多帧。
6. host receive、engine begin、handler return、commit complete、resources ready 和 submit 分开。microtasks complete 仅在建立关系时记录；请求 commit 不能记为完成。desktop observed 仍只是桌面图像观察，物理显示未测。
7. 终态区分 visible、coalesced、cancelled、stale、not_visible、rejected。合并不改变 DOM 输入投递策略；coalesced 记录和帧 coverage 共同保留最老及最新输入年龄。所有 uint64 字段导出为规范十进制字符串，JSON Number 不承载 tick 或身份。
8. 溢出、未注册、错误 owner、非法字段、时钟失败和 counter saturation 都明确导出。有丢记录的详细轮次不能用于完整因果结论；采样模式不声称完整。新同产品双提交和 burst reader 独立版本化，旧 serial-ACK/DPI/IME 校验不放宽。

## 备选方案、兼容与回退

不采用跨 DLL 任意 callback sink、热路径 lazy TLS 分配、逐事件落盘、无界日志或自动占用引擎 FIFO 的 exporter。既有 scalar DTO 和 input_packet 布局保持兼容，新 recorder 是独立诊断契约。关闭 detailed 恢复固定 scalar 开销；所有生产行为不依赖 trace 是否成功。完整 C++ 变更仍需匹配重编，旧预编译载荷不包含本实现。

## 验证交接

回归源码覆盖容量、drain、缺注册、错误 owner、stop/handle 存活、完整导出拒绝、精确身份/时钟、截断/溢出、coverage/终态和多帧关系。工具预检由专人对相同载荷做 trace 开关配对；预算见实施方案第 4 节。当前不执行构建、测试、VM 或性能工具，不据源码或 Draft PR 宣称性能通过。
