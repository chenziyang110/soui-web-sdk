# 输入到可见反馈：实施台账与专人交接

基线 `44eff565bf9ab7778a8b9fdaa5403cc3b6f568ed`，设计契约见 [ADR-0122](../adr/0122-input-to-visible-feedback.md)。遵循维护者提供的《2026-10-26 输入到 UI 性能计划》第 10 节。此台账持续更新，不把局部 timer 修复视为完整交付。

## 当前身份与依赖

实时远端核查：PR104 → PR105 → PR107 → PR108 → PR109 → PR110 均 open/unmerged；PR107–110 为 Draft。PR110 head 与指定基线一致。后续 PR 明确标 base，保持 Draft。其他工作树和 DevTools 候选未修改。

W00 已提交 [Draft PR111](https://github.com/chenziyang110/soui-web-sdk/pull/111)，base 为 PR110；源码 head `ca58a1b0f5e2e97e6df84051bdc1cb9954a956f4`。这只是第一工作包，后续输入、调度、帧/资源和交付继续推进。

## 工作包台账

| 包 | 需求与实现边界 | 回归/命令/证据 | 状态 |
| --- | --- | --- | --- |
| W00 | 同线程无效计时收益更正；continuation 生命周期/重建；legacy 单 deadline 重试；默认独立路径与 scheduler oracle | 生产代码与回归源码已集成并完成静态复核；[来源增量](../../third_party/manifests/kernel-input-ui-w00-source.json)；入口 scheduler_continuation、worker_lifetime、render_deadline_policy；未编译/未运行 | 代码完成待编译 |
| W01 | 统一QPC/身份/因果上下文；SDK+vendor冷producer、任务/FIFO/input/render调用点及严格reader | trace/terminal/backend/native-lease必要SOURCE已集成；T3b仍未知，跨geometry非空间native因果明确缺失；未执行 | 代码完成待编译 |
| W02 | 64普通+16边沿、256KiB文本mailbox/单event64KiB、32 Host公平/ordinal FIFO、显式cancel和隔离恢复 | cancel能力已在W07 guard后启用；input-mailbox/DOM/IME场景均未执行 | 代码完成待编译 |
| W03 | ready/future/idle 单次 deadline、Win7 fallback、owner机会、wait退役/失败意图；已运行Task有界退休 | scheduler_continuation真实提取源码及worker_lifetime最后Task/96项退休回归源码；最终字节映射16/15/22核对（W03包历史快照14/13/22）；未执行 | 代码完成待编译 |
| W04 | priority AVL+deadline heap摘要；50ms老化；3 eligible/1 oldest接纳；显式entry-order tie | scheduler_index 100000次seeded oracle/OOM源码、原拥有heap及shutdown fixture；未执行 | 代码完成待编译 |
| W05 | 分数frame phase、显式CPU间隔兼容、native及SDK真实show/minimize/restore接线 | fractional/host-frame/visibility SOURCE与原生窗口3文件8复制；未执行 | 代码完成待编译 |
| W06 | typed阻塞原因；生产者完成合并通知、注册后重查；移除正常1ms/8ms资源轮询 | resource_notification_policy与实际action/scene/raster链已集成；投递拒绝保留intent；未执行 | 代码完成待编译 |
| W07 | 两槽256MiB lease、有效mirror/DC sticky、锁外callback、paint/native close收据 | SOURCE与既有严格字节闭包、owner/shutdown/late callback静态复核；未执行 | 代码完成待编译 |
| W08 | raster诊断、可见/预取预算、开始前取消receipt、fenced scene恢复；默认1 worker | raster-scheduling-policy/render-scene-transaction源码；未知非空SkPicture owner在dispatch前回退worker0；未执行 | 资格基础设施完成，可用资格域与采用待数据 |
| W09 | render scene移动；Bridge/GPU日志移出热路径；const packet；分项成本与默认关闭总turn预算候选 | wake/reentrant close/terminal唯一性SOURCE；layout缓存和更细provider预算待profile | 确定性源码完成，条件采用待数据 |
| W10 | WebView2 owner/serialization诊断；默认关闭elapsed/worker prepared表示；opaque COM前后身份复验和nested pump guard | owner-policy及既有容量/导航SOURCE，actual target与credit静态审查；未编译/未真实COM | 代码完成待编译，条件采用待数据 |
| W11 | 独立ADR0126/具体GPU所有权、两在途、资源预算、device失效与软件恢复设计/诊断候选 | 当前实验路径能力/缺口与触发条件已列；不实现或启用新合格GPU后端；未实测 | 设计候选交付，待数据，未关闭 |
| W12 | 分包Draft、逐需求源码/回归入口、raw patch0015–0023、严格生成来源、暂存区清单及专人一次性矩阵 | 静态差异/语法/原字节/接口/清单核对完成；编译/运行/性能未执行 | 本阶段源码交付完成；验证与条件包仍未关闭 |

## 所有权和验证边界

A 独占 SDK 输入/引擎与 vendor 任务调度，B 独占渲染/资源/帧和 buffer，C 独占 trace API/实现与测量协议。各人独立工作树；共享 cpp 的 hook 由所属 owner 接入。集成负责人维护 ADR 索引、全局注册、来源清单、分批 PR 与最终审查。

本阶段不编译、不运行测试/VM/量测；回归源码及命令交专人。仅使用源码/差异/接口/来源的静态检查，不能推导运行正确性或性能收益。最终清单从暂存区生成，固定预编译包保持历史身份。最终代表性矩阵、一次性执行顺序和各包入口见[专人交接](../implementation/input-ui-specialist-handoff-20261026.md)，本阶段只关闭确定性源码/静态交付；编译、正确性、性能及条件采用仍未完成。

## W00 静态复核与一次性交接

- owner timer 在停止时撤销发布、清空窗口/间隔状态；native timer ID 不复用旧 owner，也不与保留 ID 相撞；SetTimer 失败取消已知 timer 并使间隔缓存失效。过期窗口、票号、关闭和 ticket 耗尽有回归源码。
- legacy 只保留一个可取消 deadline 任务；提前回调按真实剩余时间返回，dirty 在投递拒绝/OOM 时保留。resize generation 失效旧 payload，默认 independent 路由未改。首次代码审查修正了 Release 断言失效、票号回绕及长间隔兼容问题。
- W00包当时的 `tests/native/vendor_lifetime/worker_lifetime/source-map.json` 保留精确 13 个源码、12 个复制片段、22 个 payload；更新三个实际全文件身份，原部分片段保持原字节。重新核查 PR110 已变更的 writer 后，将 LLVM observer 的三个固定 writer SHA 绑定到实际 LF 规范化源码，未放宽来源校验或用新散列覆盖历史导入记录。
- 早期元数据更新因 raw/LF writer pin 不匹配两次拒绝；随后检查了实际 writer、其规范化规则与基线差异才精确重绑。没有执行生成的夹具、测试或编译。六文件原字节 patch 只做 reverse `git apply --check`，不冒充完整 forward replay 或运行证据。
- 静态依赖审计 `npm --prefix sdk/web audit --json` 本轮退出 0，报告 0 项漏洞；范围仅该包的锁定依赖，不扩大到全部 vendor 或许可结论。

验证人员一次执行两个架构的 scheduler_continuation、worker_lifetime 和[legacy deadline](../../tests/native/render_deadline_policy/README.md)入口，失败只回到相应工作包。随后真实核查提前唤醒、投递拒绝、resize/close、legacy dirty 与默认 DIRECT_HWND/independent 两条路径。当前 dormant `#if 0` custom timerqueue 分支未获安全资格，W03 将替换；不得在 W00 单独打开。W00 不证明输入完整链路或性能目标通过，后续 W01–W12 继续推进。

## W01 基础记录与协议边界

基础源码为 `sdk/{include/soui,src/runtime}/diagnostics/input_frame_trace.*` 和独立 vendor `content/common/InputFrameTrace.*`；通过 `wkeGetInputUiApi(1)` 的数值 POD 前缀连接。SDK consumer 在 `miniblink_trace_source.*` 冷固定实际模块、进程/创建时刻、QPC 频率、run/mode，每批校验 ABI、区间、slot owner 与非回退时钟。独立静态审查已覆盖保留模块的 RAII、same-tick 与 OS TID 复用、跨原子 snapshot 竞态、损坏批次整批拒绝及完整导出的反例；修正并发冷 bootstrap 幂等竞态。没有执行这些源码案例。

每进程至多32个 producer lifetime slot，每流1024项；仅相同实际 TLS cookie 可以暂停/恢复，复用 OS TID 不复用身份。详细模式需冷开启；scalar 不分配详细数组。导出在非 engine consumer，每源总量100000项；任何 loss、错误或未完全 quiescent/排空不得作为完整因果结果。新协议独立于既有 serial-ACK/DPI/IME 协议；缺少 T3b 或物理光子测量不能补值。实际生产调用点随 A/B 所有权工作包接入，基础模块存在不等于所有阶段已经可量测。

W01基础包当时静态补齐默认 vendor 注册：原64项加 provider registry-live-fence 和 input-frame-trace，共66个精确名称；preflight 原27项加两个实际 trace 源文件为29项，继续逐名/逐文件核对。SDK backend-validator 单独注册。工具尚未查询真实 CTest registry，以上只是源码注册契约，不是测试通过数。主 producer 在 wkeInitialize 真实 owner 初始化前注册，完整 platform shutdown/renderer_finalized 后才 detach；provider close fence 失败不能从 DebugBreak 继续走成功卸载。

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

独立静态复核修正组合唤醒失败、unchecked CloseHandle、reentry消耗wake、worker退休空heap及future-only重试问题；未执行任何fixture。本W03/W04包历史快照的worker原字节来源为14个完整输入、13段复制、22个payload，全部逐字节/extent/散列核对；observer绑定实际writer规范LF散列，未放宽断言。源码注册入口 `scheduler.continuation.contract`、`scheduler.deadline.{close,unregister}-failure`、`scheduler.index.contract` 在vendor选项ON下纳入根构建；该包当时66个kernel.native登记精确不变，preflight增加实际TaskSchedulingIndex为30文件；最终口径为16/15/22 worker、78+4名称、50个preflight，见SDK收口和专人交接。

## W05/W06 帧截止与资源完成

SDK phase以整数QPC商/余数得到绝对截止，vendor保持其单调秒域；迟帧直接跳至未来phase，不循环补帧。mainFrameRate把同一个目标间隔传到record/composite；已按目标接纳的frame不再叠加独立last-record/composite/legacy绘制等待。Host只在匹配的cap0x10上采用fractional策略，显式legacy实验/整数override保留原语义；旧毫秒诊断字段仍是兼容取整值，实际调度/收益以QPC记录和真实呈现为准。

资源阻塞分为destroying/inflight-scene/missing-action/raster，pacing另行处理。owner先登记等待再重查，action发布/完整frame应用、scene激活和raster完成触发合并通知；任务持有撤销状态与代际，关闭后的迟到任务只回收自身。正常1ms commit和8ms draw资源轮询移除；一次100ms异常拒投兜底仍失败时保留dirty/等待intent，后续真实owner机会重试，不把取消当resources ready。

本批静态发现native hide/minimize/restore没有调用原 `setVisible`，因此可见性抑制/恢复全量dirty不能仅凭已有方法宣称接入。后续组合包通过可选原生能力和实际窗口状态补齐，再冻结完整W05；DPI已有实际scale/resize链，与此缺口分开记录。W07的镜像/lease和W01调用点尚未在本批激活。

默认vendor登记本批为69个kernel.native＋4个scheduler辅助名称，preflight33个确切源文件；既有legacy deadline leaf纳入根登记，另增fractional-frame-phase与resource-notification-policy。命令交专人执行，当前只交生产代码、必要fixture源码与静态来源检查。

静态集成复查同时修正 owner API 门禁引用未声明 prepared 标志的问题：只有 worker retirement 与 broker fence 均实际就绪时才发布 prepared，未就绪继续保留 owner 关闭进展。该更正纳入本批原字节增量，仍未编译。

## 呈现、输入场景与关闭组合

W07的真实buffer lease在CPU/GDI/client调用期间保持像素和paint lifetime；release通知/post结束后才放弃activity。镜像只在已验证DIRECT_HWND且无DC/透明/legacy需求时关闭；DC sticky和有效需求查询不能由后来false覆盖。可变CC树walk仍有锁，未宣称全部长锁消除。

Root WKE胶水先按实际OS owner检查，再执行view/window permit、endpoint失效、once destroy callback、native window preflight及activation移除/delete。native DestroyWindow失败、HWND身份丢失或复用保留失败收据；不强删。preparePaintShutdown位于worker/broker prepared发布之前；有活动/排队析构/native故障时继续pending，重复prepare仍走真实owner和实际收据。cancelInput能力仅在外层activity guard接上后开放。

独立wheel/barrier在发布预测之前传入actual WKE view ID和原始view/document/geometry tuple；active scene本身绑定confirmed base identity，unknown/mismatch回退原输入mailbox。compositor receipt序号与router input序号独立。事务预分配失败无预测副作用，发布成功后唤醒失败仍是accepted；queued draw的计数由自身实际run/dispose退休，close不清零伪装排空。SDK applied tuple门禁随后续W01接入包闭合。

Native visibility setter只在owner调用；hide保留dirty/action/资源/scene/scroll fence并抑制新渲染，restore重建phase和全量composite damage，以一个立即、可取消owner wake恢复；拒投保留intent。Host真实child/root最小化采样随后续SDK包接入，不把setter返回成功当桌面已显示。

原逐事件interaction文件写和GPU present失败时重复文件格式化已改为stage29/30固定数值诊断。why/outcome/flags/关联字段全0，value限固定20/4枚举域；recorder、backend、reader均拒绝伪造关联。frame_id仍是main/scene record身份，同一ID可有多次真实T6；原始stream/producer/wire记录保留，不能按ID去重或推导跨线程同tick顺序。

可选API逐字段按version/structSize/cap/null/OK0检查：mirror0x4、input context0x8、cancel0x20、confirmed view0x40、active-scene gate0x80、visibility0x100、external producer0x200。外部producer只冷租用，调用线程配对释放；借用已有内部writer要求内部loop存活覆盖借用期，SDK UI持有实际DLL引用至最后hot borrow退出后释放。没有热路径自动注册。

本批新增根登记为78个kernel.native精确名称＋4个scheduler辅助名称、50个preflight源文件；native window/visibility/wheel/diagnostic fixture按当前生产字节提取并明确依赖fake，不等于真实Win32/GL/COM通过。旧constructor/drag来源门槛也必须随实际变更闭合，不能只刷新SHA掩盖旧片段。

原生窗口wrapper也已接到真实状态：CWebWindow show、WM_SHOWWINDOW、WM_WINDOWPOSCHANGED(default先行)、WM_SIZE均在whole activity期间采样实际attached HWND/root iconic，并核对LTH状态后才合并同ack。最小化不覆盖有效viewport，外部CAPI改变visibility会使旧ack失效。沿用window-close-lifetime-source同一入口，来源现为3个文件/8段复制；未运行。

## SDK因果接线与采用边界

Render/close/visibility组合已冻结于 [Draft PR120](https://github.com/chenziyang110/soui-web-sdk/pull/120)，依赖PR119。当前SDK包补齐真实T1/admission/T2/T3a、Engine FIFO/Bridge/native TaskPair/Timer上下文、base identity实际docready+resize确认和独立输入applied tuple门禁；不伪造microtask完成、T7或光子时间。native UI producer租约只在成功initialized process module之后安装，模块身份/函数/版本/大小/cap均核对；hot scope跨最后unmount保持实际调用所有者。

SDK输入handler为冷分配共享拥有对象，锁内只copy pin、锁外call/destruction。recovery terminal转移到in-flight local，重入close不重复签终态；迟到empty wake不能毒化后续post，每次pending仍用原auto-reset event合并。SDK实际核心文件已与作者最后审过版本逐字节核对；补齐跨W10共享的trace_thread_borrow定义、直接engine startup目标中的actual trace.cpp，以及native UI lease根登记。

W09默认预算0/off；显式100..5000us只协调协作批次，单个provider/JS回调仍不可抢占，细化跨provider剩余预算等profile。Bridge逐事件开文件/日志专用格式化与owner pump重复GPU日志读均移除；GPU状态只取本child HWND的实际属性，缺证据用CPU保守策略，不取其他surface进程全局实验值。

worker严格来源为16个完整输入、15段复制、22个payload。仅对已独立审过的两份C trace原件/whole-copy更新pins，其他原件/片段/payload/关闭proof全部精确一致；LLVM observer新增6个严格角色已补齐：5个固定writer recipe的46个输出路径、8个实际编译对象，包含wheel的两个generated.cpp；index只允许0生成。每个来源/对象对先由真实Ninja绑定选择，再从冻结writer+原字节推导，拒绝未知source和build后manifest授权。

SDK本批LLVM观察器已静态重绑16/15/22 worker形状与3个canonical LF writer SHA；原有未知来源/篡改拒绝保持。新增SOURCE覆盖缺失/错配compiler unit、额外object、未知header/manifest及writer/pin篡改。只完成PowerShell语法树解析、原字节/散列/源码差异检查，没有执行脚本主体、SOURCE案例或编译。host_visibility复制均为UTF8（AutoRecord含非ASCII），使用/utf-8；window native_close片段保留CP936，其余没有以重新编码冒充原件。

## WebView2独立包

[ADR-0127](../adr/0127-webview2-owner-output.md)记录可选输出预算/表示、实际ComPtr目标、get_Source opaque callout前后身份、nested pump guard和回退。原16项上限、来源/会话/代际/授权、lane及64KiB credit不扩张。两项性能候选默认关闭，真实Runtime版本、COM存活、输入公平性和CPU/serialization成本由专人冻结profile；不能用policy/fake SOURCE替代真实COM。当前renderer全部改动与作者最后已审代码逐字节一致，根已登记sdk.native.webview2-owner-policy。

## 最终交接

软件源码已按Draft111→116→117→118→119→120→121→122分批冻结；独立GPU/交接分支为codex/input-ui-gpu-handoff-20261026。完整head、来源、用例入口、命令、一次验证/失败停止规则与条件待数据项统一见专人交接；最终PR自身head由执行前git记录，不能用旧worker hash代替。本轮无配置/编译/运行/VM/性能数据，不宣称极致流畅度、完整生产验收或所有优化完成。
