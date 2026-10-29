# MiniBlink 调度候选交接（2026-10-29）

基线：`c17c79bdf470fae681c63324ceaa2a519151afc9`（父任务建立的原工作树源码快照）。本文件仅记录本 lane；未提交、未编译、未运行测试。实施目录为隔离工作树，不覆盖原任务。

## 已写代码

- `WebThreadImpl.*`：单调调度时间；2ms / 32 callback 合作式批次；跨线程任务只搬运有界 owner batch，未搬完部分有明确所有者；工作线程按实际 pending/deadline 等待；main idle 无固定 16ms tick，ready 与 future 分别唤醒；enableScheduler 重新建立唤醒。
- `SharedTimerWin.h`：Win7 原生 threadpool timer，可重复 arm/disarm；独立的平时未 arm 的 PostMessage 失败重试 timer。跨线程回调只投递 owner 消息，不运行 Blink，不从 producer 对 owner HWND 调 SetTimer。owner 压力回退使用带代际票号的一次 WM_TIMER。取消/重 arm 清理 deferred；WM_QUIT 抑制自补 ready 链；恢复广播复核 deadline。stop 先封闭发布，锁外 cancel/wait/close 两个 callback 对象，再销毁窗口。
- `TaskSchedulingPolicy.h`：基于已有 deadline heap 剪去 future 子树和不可能胜过已老化最早任务的子树；保留原 50ms 老化、优先级、heap index 同键 tie-break。未引入第二个 ready 索引或无锁结构。
- `WebTimerBase.cpp`：内部创建、比较与测量统一为单调时钟。移除不影响原同线程 deadline 的 `m_currentFrameCreateTime` 字段和传参，避免继续描述无效收益。
- ADR：`docs/adr/0137-miniblink-deadline-scheduler.md`。
- 后续输入保障：`sdk/src/host/input_admission.h`、`engine_admission.h`、Engine 私有引用接纳；开始前预留普通 ending/blur credits，结束仍同 FIFO。失败 callback 由 caller 在 input lock 外析构；guarded admission 在同一次 Engine 锁内核对全部结束 ticket 的 epoch。accepted blur 释放该 Host 剩余 lease；view/document/Engine 变化和 unmount reset，geometry 不撤销 lease。完整规则见 ADR 0141，Host 由集成负责人接入。

## 回归源码与静态检查

- `tests/native/scheduler_fairness_tests.cpp`：500 个 deadline heap 与原线性策略 oracle 对照（普通/排空），16383 个 timer 但只有一个 ready 的遍历次数，以及不得提前的优先级查询。
- `tests/native/input_admission_tests.cpp`：真实 Engine full 下 start/end FIFO 与 ending credits、重复 down、geometry、focus/blur、IME 多 result、代际/epoch、reservation 回滚、capture 析构与 callback reset 重入、多 Host。已交由 root 注册 `sdk.native.input-admission`，未运行。
- `tests/native/vendor_lifetime/worker_lifetime/fixture.cpp`：main 初始无周期 timer、真实 immediate、25ms delayed 的实际任务执行时戳、deferred 后取消/未来重 arm、旧已入队 WM_TIMER 票号拒绝、后续 ready 可进展、WM_QUIT 可被取出后 accepted task 在 shutdown 排空。局部 TaskReceipt 存活到真实 timer 析构，避免 fixture 自造 UAF。
- fixture 的 `fixture_support.h` 只增加单调时钟依赖投影；Win32 threadpool timer 是真实系统 API；`source-map.json` 按实际字节重新绑定，未改片段 SHA 保留。
- `git diff --check` 对本 lane 文件通过；源码字节映射的静态 hash/片段检查通过。没有运行 CMake、编译器、Node 测试、CTest、VM 或性能采样。

独立静态审查由 WebView2/Scratch lane 执行，指出并修复了 deferred 取消丢醒、旧 WM_TIMER alias、WM_QUIT 饥饿、跨线程 SetTimer 以及过期 timer-queue period=0 无法复用的 API 边界；改用原生 threadpool timer 后复读未再发现具体阻断。末次 init 失败回滚与过期即 ready 分支仍交给集成者复查。

## 集中验证入口（未执行）

先构建匹配的新内核和 SDK，再按正常工具链执行相关测试。已有 CTest 名：

- `kernel.native.scheduler-fairness`
- `sdk.native.input-admission`
- `kernel.native.worker-lifetime.main-thread-compatibility`
- `kernel.native.worker-lifetime.*`（已接纳任务排空、取消、所有权、真实退出与故障边界）

例如已配置构建目录下 `ctest --test-dir out/x64 -C Release -R '^kernel.native.(scheduler-fairness|worker-lifetime\.)' --output-on-failure`，仅供验证人员执行；实际目录与生成器依其构建入口确定。两架构、MSVC 和显式 Win7 LLVM 档位必须核对 `_WIN32_WINNT >= 0x0600` 的声明与最终 Kernel32 导入，不混换工具链当作算法收益。

真实 main producer burst、持续 native pressure、native queue quota/PostMessage 失败重试、睡眠恢复、取消/重 arm/关闭竞争、窗口重建/HWND 复用，以及 DevTools 断点暂停→单步→继续→关闭仍需匹配产物的集中过关。fixture 不代表完整 Blink/GC/真实桌面/性能/稳定性认证。

## 明确未关闭

1. 没有修改 SDK engine FIFO 或占用 reply/renewal/cleanup 预留。新 helper 在接受开始之前持有结束容量，覆盖已接受的 key/pointer up、首 IME commit/end 与 blur。额外 IME result、孤立 release、普通字符仍可能真实 full，不声称恢复。Host 的真实 dispatch 不能仅因 geometry 变化而静默丢掉已接受的非位置结束事件；该集成由 root 审核。
2. MiniBlink producer vector 的原有存储容量未改为新硬上限；本补丁限制的是搬运/执行时间与项数，不是接受任意任务的总容量。公开 void postTask 的拒绝/恢复语义需要单独契约，不能静默丢任务。
3. 新鲜 ready timer 很多时仍需访问 ready 子树；完整 ready/future 索引分离属条件候选。没有数据不添加复杂重复索引和更新风险。
4. 单个 JS/GC/client callback 仍可长时间阻塞，本补丁不抢占它。所有平台调度/定时精度与最后屏幕呈现均未测。
5. 压缩包/解压运行文件/内核运行载荷净体积门禁尚未运行。无新 DLL 不等于零字节增长；此状态不能写“体积通过”或“全部性能目标完成”。

私有 `WebThreadImpl` 布局有变化，必须匹配重编，不能把旧 DLL 的结果挂到本源码。来源/补丁总清单和最终交付机器身份由集成负责人统一刷新。
