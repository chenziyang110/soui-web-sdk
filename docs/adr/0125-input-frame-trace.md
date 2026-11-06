# ADR-0125：有界的输入、任务与可见帧关联记录

- 日期：2026-10-26。
- 状态：实施决定；原始交接仅含源码和静态审查，本次桶索引的限定实际验证见末节。
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
9. 冷启动 storage 分配/构造失败只使诊断 available=false，不允许终止应用或 DLL 初始化；缺失 storage 的所有操作安全拒绝。默认 scalar 身份仍冷采实际 PID/FILETIME/QPC frequency，run_id 保留零直到显式 configure。exporter 每会话最多 100000 事件，超限标 causal_lost、limit_reached 并拒绝 complete end，不创建无界日志。

## 备选方案、兼容与回退

不采用跨 DLL 任意 callback sink、热路径 lazy TLS 分配、逐事件落盘、无界日志或自动占用引擎 FIFO 的 exporter。既有 scalar DTO 和 input_packet 布局保持兼容，新 recorder 是独立诊断契约。关闭 detailed 恢复固定 scalar 开销；所有生产行为不依赖 trace 是否成功。完整 C++ 变更仍需匹配重编，旧预编译载荷不包含本实现。

### 2026-10-29：保持样本的桶索引细化

SDK 的 64 桶索引以私有 C++17 helper 计算：`ticks` 为 0 或 1 时返回 0，其余返回 `floor(log2(ticks))`。MSVC/clang-cl 的 x64 使用位扫描，x86 先检查高 32 位；其他平台固定六步二分，避免原逐位右移的最多 63 次循环。零值不进入要求非零参数的内建操作。没有新增运行时依赖、Windows 导入或 CPU 特性要求。

该调整保留原 QPC、scalar/detailed 样本、所有 metric、无注册计数、错误 owner、停止和饱和处理。默认 scalar 不是关闭诊断，因此不通过跳过 QPC、删除样本或修改线程注册来获得收益。vendor 源码和内核保持原实现，其桶边界相同；此项验证只证明算法与计量语义，不构成 FPS 或输入可见延迟结论。

## 原始验证交接（2026-10-26）

回归源码覆盖容量、drain、缺注册、错误 owner、stop/handle 存活、完整导出拒绝、精确身份/时钟、截断/溢出、coverage/终态和多帧关系。工具预检由专人对相同载荷做 trace 开关配对；预算见实施方案第 4 节。原始交接阶段未执行构建、测试、VM 或性能工具，不据源码或 Draft PR 宣称性能通过。

## 2026-10-29 桶索引限定验证

- 在 `c4ba38b7` 加显式源码快照上，先给原算法运行新增边界回归，再替换 SDK 算法；x64 使用现有 SDK 构建，x86 另建独立三编译单元夹具，均为 Release、显式 LLVM22/MSVC142、Windows SDK 19041。两架构前后各一次 `kernel.native.input-frame-trace`，实际均 1/1、自然退出 0/0，无强制结束；并非完整 x86 SDK 构建或默认 MSVC143 资格。
- recorder 回归在 SDK/vendor、scalar/detailed、全部 15 个 metric 上验证 195 个零值/最大值/二进制边界样本，共 11700 次 duration 写入；同时验证错误 owner、非法 metric、缺注册、stop 后拒绝及 producer 释放。候选另将 native 与 portable helper 的 100000 个确定性样本和旧逐位算法对照。所有调用仍保留原 QPC 与计数。
- 实际候选 OBJ 的 duration 方法中，x64 使用 1 条 BSR，x86 使用 2 条 BSR，无 LZCNT/TZCNT/POPCNT；原逐位回跳消除。x64 SDK 静态库从 14199244B 变为 14199190B（-54B）；尚未据此重新编译完整应用或重封 ZIP，不能推广为运行包体积结论。
- 首次 x86 尝试因不存在预期缓存而在编译前失败，第二次被标准配置 helper 的 workspace/out 边界拒绝；均保留。随后在全新 workspace/out 夹具目录按同一工具链完成实际构建与运行，没有放宽原 helper。

原始监督证据键及 SHA-256：

| 证据 | SHA-256 |
|---|---|
| `native/trace-bucket-x64-baseline-r1/result.json` | `6ede72f43bf3f08519e60e721082798d11f26ebf08a7e06573af8dd777d50a3d` |
| `native/trace-bucket-x86-baseline-r3/result.json` | `ffb7b5ceeb20b34469822c3f74739704d66c8bb8a767c062f58bd21d7d8cf38a` |
| `native/trace-bucket-x64-candidate-r1/result.json` | `9e9ecb2b613ea118f8e72dc953ecfebc6a853e2aa299e38f542711293807d25a` |
| `native/trace-bucket-x86-candidate-r1/result.json` | `0d00efa1b1f4f7f9541d97a2efa8ed8bfafbfea25c674912b6fe51361b15a41c` |
| `native/trace-bucket-codegen-r1/result.json` | `9d88e54ad56e3778c3b828ffab113236d4bb7d810c0106476f9bc887a96c936d` |

随后用同一SDK库重链实际x64 MiniBlink手动消费者，`manual-preview-candidate-x64-r3`在6596ms自然0/0，编译输入前后SHA一致。EXE为1506816B，SHA `ebb4aa0f93247ebd86cc716f9f256f3a13955c033d0f229b0e8f31a0cfbb52ba`。受支持的独立runtime部署入口校验同一页面、同一内核后，r2/r3按同一清单口径都为45856205B，EXE和runtime增加0B；没有重新打完整Scratch ZIP或启动r3。SDK静态库差值不能外推到其他消费者或完整包。

这些结果证明桶算法等价和所列生命周期边界；没有测得页面 FPS、trace 扰动预算或物理输入可见延迟，不更新性能资格。
