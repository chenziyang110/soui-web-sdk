# 输入到可见反馈：实施台账与专人交接

基线 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`，设计契约见 [ADR-0122](../adr/0122-input-to-visible-feedback.md)。遵循维护者提供的《2026-10-26 输入到 UI 性能计划》第 10 节。此台账持续更新，不把局部 timer 修复视为完整交付。

## 当前身份与依赖

实时远端核查：PR104 → PR105 → PR107 → PR108 → PR109 → PR110 均 open/unmerged；PR107–110 为 Draft。PR110 head 与指定基线一致。后续 PR 明确标 base，保持 Draft。其他工作树和 DevTools 候选未修改。

W00 已提交 [Draft PR111](https://github.com/chenziyang110/soui-web-sdk/pull/111)，base 为 PR110；源码 head `ca58a1b0f5e2e97e6df84051bdc1cb9954a956f4`。这只是第一工作包，后续输入、调度、帧/资源和交付继续推进。

## 工作包台账

| 包 | 需求与实现边界 | 回归/命令/证据 | 状态 |
| --- | --- | --- | --- |
| W00 | 同线程无效计时收益更正；continuation 生命周期/重建；legacy 单 deadline 重试；默认独立路径与 scheduler oracle | 生产代码与回归源码已集成并完成静态复核；[来源增量](../../third_party/manifests/kernel-input-ui-w00-source.json)；入口 scheduler_continuation、worker_lifetime、render_deadline_policy；未编译/未运行 | 代码完成待编译 |
| W01 | 统一 QPC/身份/关联；默认关闭有界 ring；scalar 指标；替换逐事件实验文件日志；同产品与 burst 新协议 | 公共数值契约冻结中；严格旧协议保留；无执行结果 | 实现中 |
| W02 | 64普通+16边沿 mailbox、32 Host轮转、与FIFO共享接纳序号；接纳后resize代际发布、序号不回绕；取消不制造gesture | input_admission生产源码fixture、ordinal_cases及真实DOM/resize失败场景已交付；未编译/未运行；native cancel能力等W07 callback guard才开放 | 源码已集成，能力激活依赖W07 |
| W03 | ready/future/idle 单次 deadline、Win7 fallback、owner机会、wait退役/失败意图；已运行Task有界退休 | scheduler_continuation真实提取源码及worker_lifetime最后Task/96项退休回归源码；字节映射14/13/22核对；未执行 | 代码完成待编译 |
| W04 | priority AVL+deadline heap摘要；50ms老化；3 eligible/1 oldest接纳；显式entry-order tie | scheduler_index 100000次seeded oracle/OOM源码、原拥有heap及shutdown fixture；未执行 | 代码完成待编译 |
| W05 | 复用 pacer 和分数相位；窄版本能力/代际契约；资源与 pacing 分离；首次 dirty/窗口状态 | 可控时钟与刷新率/恢复/跨屏回归源码待交付 | 待 W01 接口 |
| W06 | 原因分类；资源完成合并通知；注册后重查；移除正常 1ms/8ms 轮询，完整 fence | 完成竞态/多 producer/迟到/取消/资源释放回归源码待交付 | 待 W05 |
| W07 | copy/锁/回调计量；mirror 消费者；有界 generation buffer lease；锁外绘制 | 像素/alpha/resize/DC/截图/重入/关闭回归源码待交付 | 待 W05/W06 |
| W08 | raster 成本诊断；可见优先/有界并发候选；项数/字节/scene/cache/预取预算 | 未 profile；不得把缺数据记为不采用证据 | 待设计/诊断/候选与数据 |
| W09 | layout/record/scene/snapshot/JSON/Bridge/分配诊断；按证据合并、切片、失效缓存及预算协调 | 未 profile；不无依据大改 layout | 待设计/诊断与数据 |
| W10 | WebView2 输出批次实耗时、转换/JSON/deque/广播诊断；COM/授权/撤销保持 | 未 profile；Runtime 外部内部调度另列边界 | 待设计/诊断与数据 |
| W11 | GPU 独立设计/诊断候选；device loss/生命周期/深度/软件回退 | 软件主线和 profile 尚缺，未启用 | 待数据，未关闭 |
| W12 | 精确需求→源码→回归源码→命令→证据→状态；原编码补丁/来源/注册；暂存区清单；分批 Draft PR | 由集成负责人随各包刷新；所有未执行保持未执行 | 持续集成中 |

## 所有权和验证边界

A 独占 SDK 输入/引擎与 vendor 任务调度，B 独占渲染/资源/帧和 buffer，C 独占 trace API/实现与测量协议。各人独立工作树；共享 cpp 的 hook 由所属 owner 接入。集成负责人维护 ADR 索引、全局注册、来源清单、分批 PR 与最终审查。

本阶段不编译、不运行测试/VM/量测；回归源码及命令交专人。仅使用源码/差异/接口/来源的静态检查，不能推导运行正确性或性能收益。最终清单从暂存区生成，固定预编译包保持历史身份。完整代表性验证矩阵、一次性执行清单与每包命令将在对应实现冻结后补齐，未齐前不称代码交付完成。

## W00 静态复核与一次性交接

- owner timer 在停止时撤销发布、清空窗口/间隔状态；native timer ID 不复用旧 owner，也不与保留 ID 相撞；SetTimer 失败取消已知 timer 并使间隔缓存失效。过期窗口、票号、关闭和 ticket 耗尽有回归源码。
- legacy 只保留一个可取消 deadline 任务；提前回调按真实剩余时间返回，dirty 在投递拒绝/OOM 时保留。resize generation 失效旧 payload，默认 independent 路由未改。首次代码审查修正了 Release 断言失效、票号回绕及长间隔兼容问题。
- `tests/native/vendor_lifetime/worker_lifetime/source-map.json` 保留精确 13 个源码、12 个复制片段、22 个 payload；更新三个实际全文件身份，原部分片段保持原字节。重新核查 PR110 已变更的 writer 后，将 LLVM observer 的三个固定 writer SHA 绑定到实际 LF 规范化源码，未放宽来源校验或用新散列覆盖历史导入记录。
- 早期元数据更新因 raw/LF writer pin 不匹配两次拒绝；随后检查了实际 writer、其规范化规则与基线差异才精确重绑。没有执行生成的夹具、测试或编译。六文件原字节 patch 只做 reverse `git apply --check`，不冒充完整 forward replay 或运行证据。
- 静态依赖审计 `npm --prefix sdk/web audit --json` 本轮退出 0，报告 0 项漏洞；范围仅该包的锁定依赖，不扩大到全部 vendor 或许可结论。

验证人员一次执行两个架构的 scheduler_continuation、worker_lifetime 和[legacy deadline](../../tests/native/render_deadline_policy/README.md)入口，失败只回到相应工作包。随后真实核查提前唤醒、投递拒绝、resize/close、legacy dirty 与默认 DIRECT_HWND/independent 两条路径。当前 dormant `#if 0` custom timerqueue 分支未获安全资格，W03 将替换；不得在 W00 单独打开。W00 不证明输入完整链路或性能目标通过，后续 W01–W12 继续推进。

## W01 基础记录与协议边界

基础源码为 `sdk/{include/soui,src/runtime}/diagnostics/input_frame_trace.*` 和独立 vendor `content/common/InputFrameTrace.*`；通过 `wkeGetInputUiApi(1)` 的数值 POD 前缀连接。SDK consumer 在 `miniblink_trace_source.*` 冷固定实际模块、进程/创建时刻、QPC 频率、run/mode，每批校验 ABI、区间、slot owner 与非回退时钟。独立静态审查已覆盖保留模块的 RAII、same-tick 与 OS TID 复用、跨原子 snapshot 竞态、损坏批次整批拒绝及完整导出的反例；修正并发冷 bootstrap 幂等竞态。没有执行这些源码案例。

每进程至多32个 producer lifetime slot，每流1024项；仅相同实际 TLS cookie 可以暂停/恢复，复用 OS TID 不复用身份。详细模式需冷开启；scalar 不分配详细数组。导出在非 engine consumer，每源总量100000项；任何 loss、错误或未完全 quiescent/排空不得作为完整因果结果。新协议独立于既有 serial-ACK/DPI/IME 协议；缺少 T3b 或物理光子测量不能补值。实际生产调用点随 A/B 所有权工作包接入，基础模块存在不等于所有阶段已经可量测。

静态补齐默认 vendor 注册：原64项加 provider registry-live-fence 和 input-frame-trace，共66个精确名称；preflight 原27项加两个实际 trace 源文件为29项，继续逐名/逐文件核对。SDK backend-validator 单独注册。工具尚未查询真实 CTest registry，以上只是源码注册契约，不是测试通过数。主 producer 在 wkeInitialize 真实 owner 初始化前注册，完整 platform shutdown/renderer_finalized 后才 detach；provider close fence 失败不能从 DebugBreak 继续走成功卸载。

专人先运行 trace/backend validator、精确 registry 和新协议的 reader fixture，再做一次 trace scalar/detailed 扰动预检。关联 reader 必须检查实际阶段交集与样本数；SDK/vendor/API 版本失配、其他 consumer 取走记录、trace 超限均不得生成合格的 complete capture。随后才进行同产品配对；未冻结的生产调用点不提交正式性能结果。

## W02 输入接纳与代际屏障

`sdk/src/host/{input_mailbox,engine_input_service,engine_admission,input_sequence}.h` 与实际 `engine_thread.cpp` 共同保证：payload 复制成功后，分配顺序号与首 head/pending 发布在同一次 Engine mutex 事务完成；Engine 锁内只读原子 head，不进入 mailbox。普通 FIFO 不越过更早已接纳输入，输入 batch 每次 pop 重新核对更早的 FIFO，32 个 actor 轮转且单次至多8项/1ms协作预算。新输入不能无限延长某个已排队任务的前驱集合；任意单个 JS/原生回调仍不可抢占。

resize 排队失败不推进 UI 代际、序号或窗口尺寸；成功入队后才发布代际并 MoveWindow，引擎只在实际 wkeResize 返回后推进 applied geometry。序号跨 geometry 单调，view/document 更换才可重新开始；耗尽/有界争用失败明确进入取消/有限恢复，不回绕复用。旧 geometry 的 key/char/IME 保留投递，空间输入按实际几何校验；W01 对跨几何非空间输入的 native frame 因果保持明确缺失，不改写历史身份。

静态审查修正了独立队列跨resize/nav、首head发布窗口、全部IMM错误被当空提交、UI意图误判旧已接纳输入及resize接纳失败后永久stale等问题。`WebPageImpl::cancelInputState` 先有界预检最多128个本地frame，再清原生click/drag/touch、取消未提交composition并blur；不合成 keyup/mouseup。中间 Draft 在 callback lifetime 依赖闭合前不公布新取消能力，缺能力时明确有限恢复至新文档。不得把编写了取消方法等同已经具备关闭/重入资格。

执行入口：构建最终匹配 SDK 后一次运行 `sdk.native.input-mailbox`；实际 DOM/held key/drag/真实IME及满ordinary resize失败见 `tests/native/input_frame_trace/INPUT-CANCEL-SCENARIOS.md` 与 `tests/native/input_admission/RESIZE-FAILURE-SCENARIOS.md`。后两者是待执行场景，不能算成已运行的原生案例。当前所有运行证据为空。

## W03/W04 deadline、索引与退休

两个工作包按独立实现提交，在同一 Draft 中交接，因为等待票号、最早deadline摘要和最后任务析构共同决定 idle 是否可安全停止。`SharedTimerWin.h` 不再用固定16ms驱动空闲owner；一份绝对单调deadline、一份注册wait及同票号WM_TIMER故障兜底。owner重新检查到期条件；注销wait在publication锁外等待callback退役，句柄失败不能清空后误报成功。消息投递失败保留deferred intent，全部计时原语失败保留独立deadlineRetry，由实际owner机会重试；所有OS唤醒均失败时仍需要外部pump机会，不能承诺硬实时。

删除idle tick还必须处理原先靠下一tick释放的已运行Task：回调/observer返回、重入计数归零后每次最多退休32个，先移除拥有记录再锁外deref，剩余显式续调度。worker无heap、只有退休积压的wake也会实际处理，关闭仍保留完整排空；不能让最后close Task的析构收据永久挂起。

`TaskSchedulingIndex.h` 非拥有索引保留原heap/refcount；priority AVL节点缓存子树最早deadline，查询最高可运行priority或50ms老化的最早deadline，不扫描全部future或先做无界promotion。exact equal key从旧encounter顺序明确改为queue-entry顺序；原不透明Task/global producer容量协议仍是既有限制，本包未用新增无界队列换取性能。所有新索引分配先于timer状态变更，结构容量计量包含保留bucket高水。

独立静态复核修正组合唤醒失败、unchecked CloseHandle、reentry消耗wake、worker退休空heap及future-only重试问题；未执行任何fixture。当前worker原字节来源为14个完整输入、13段复制、22个payload，全部逐字节/extent/散列核对；observer绑定实际writer规范LF散列，未放宽断言。源码注册入口 `scheduler.continuation.contract`、`scheduler.deadline.{close,unregister}-failure`、`scheduler.index.contract` 在vendor选项ON下纳入根构建；66个kernel.native登记仍精确不变，preflight增加实际TaskSchedulingIndex为30文件。
