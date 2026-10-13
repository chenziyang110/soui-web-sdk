# 0055：参考应用的有限原生只读观察

状态：已通过 Root 与独立代码审查并采纳，实际应用尚未编译和执行。只增加应用私有观察，不修改 SDK 或参考应用动作协议。

## 背景与决定

当前三个实际应用的 `--smoke-test` 路径会等待 applicationReady 和真实 GUI 文档绑定，然后按既有顺序关闭。固定 host.log 行可以在精确源码绑定下证明该分支被执行，却没有实际 PID/FILETIME、sessionId、Compat execution-context receipt 或 Modern session digest，不能当成数字收据。为独立消费验证增加应用自己的 opt-in 诊断文件；不新增 app mode、页面 API、授权回调、动作或 GUI 调度能力。

`application` 构造时仅一次读取 `SOUI_REFERENCE_OBSERVATION=1`；其他值或未设置均关闭。打开后，在既有成功点写 `profile/app-observation.ndjson`。普通 host.log 保持。诊断异常被捕获，原关闭流程继续；外部 reader 必须拒绝缺行、损坏行、null 身份、重复阶段或实际失败，不能据日志补造字段。所有记录 `nonAtomic=true`，不表示整个状态在一个锁下成立。

## 字段来源与时点

| 字段 | 实际来源与采样时点 |
| --- | --- |
| schema/phase/sequence | 固定 schema、固定调用点、本 application 内递增计数；不是外部输入 |
| pid/creationFileTime | 每次记录直接读取 GetCurrentProcessId/GetProcessTimes，FILETIME 为十进制字符串；读取失败为 null |
| qpc/currentThread | 每次记录直接读取 QueryPerformanceCounter/GetCurrentThreadId；QPC 失败为 null，reader 不得构造替代时间 |
| ownerHwnd/ownerThread/ownerProcess/ownerValid | 仅撤销前的已有成功点读取现有 `window` 的数值和 GetWindowThreadProcessId/IsWindow。begin 尚无 HWND；bridge-join-complete、runtime-shutdown-complete、native.end 三个撤销后固定阶段不查询旧 window 数字，ownerHwnd/thread/process 明确为 null、ownerValid=false。app.window 与原销毁/循环/close 行为不改变。绑定阶段要求同进程、同 owner 线程和真实有效 HWND；不是借此获取或控制其他窗口 |
| closing/guiBound/exitCode | 记录时现有 application 标量，不重新调用 Host 或权限逻辑 |
| sessionId | 仅 GUI bind 成功记录传入原分支已捕获的真实 `host->session()->session_id()`；其他阶段为 null，不回读或保留已失效对象 |
| contextReceipt | 仅 Compat bind 成功记录，从原分支真实 `execution_context_receipt()` 构成的 `generation` 取数值；原非零/accepts check 和 bind 调用完全不变。不是新的或续期 receipt |
| sessionDigest | 仅 Modern bind 成功记录，从原分支真实 `session_digest()` 构成的 `generation` 取值；SDK 将真实 sessionId 的 digest 保存在适配器锁下。不是 bearer、scope token 或 wire 授权 |
| renderer/runtimeVersion | renderer 由原编译分支固定；本最小候选不增加 runtime 查询，runtimeVersion 永远 null，不能用于核实实际 Evergreen 运行时。后者需要 Root 独立 runtime/loaded-image 身份证据 |
| proofRecheckedForObservation | 固定 false。原 gui_current proof 的次数、短路顺序、native bind、Agent start 与关闭顺序不变；记录不重新执行 proof |
| qualificationPassed/releaseReady | 固定 false。记录是有限观察，不提升生产、OS、物理输入、光学或呈现资格 |

## 固定阶段与生命周期

1. `native.begin` 在 profile 目录真实创建后、业务/Bridge/窗口创建前。
2. `application-ready` 紧随原 ready 成功日志；不再调用 application_ready。
3. `gui-native-document-bound` 在原 `gui_current` 与 `bind_gui_document` 均成功、`gui_bound=true` 后。捕获的 session/receipt/digest 只在这次同步记录期间借用；无异步存储。
4. Agent 显式启用时，`agent-ready-native-document-bound` 紧随原真实 start_bound_document 成功点。smoke 模式不会启用 Agent。
5. `gui-stop-complete`、可选 `agent-stop-complete`、`host-close-complete`、`bridge-join-complete` 仅紧随原相应成功点。pending 保留原 Host/adapter/Bridge/HWND/模块，不记录成功；Bridge join 记录发生在真实 Bridge 析构之后。
6. Compat 的 `runtime-shutdown-complete` 仅原 shutdown 返回成功后。Modern 没有此 Miniblink 阶段，不能要求或补造。
7. `native.end` 在 run 即将返回时记录 exitCode；它不是 OS 进程退出、CoUninitialize 完成或整个 Job 自然闭合的替代证据。

外部验收仍须直接绑定 Owned PID/字符串 FILETIME、原始 root/terminal/context、自然 0/0、无 force/limit/fallback/signal、真实所有成员闭合。只有记录和自然进程结果同时满足要求，才可报告当前三应用的有限 smoke；编译 0/124 继续保持 false/partial。

## 兼容与迁移

该候选只增加 app 私有诊断逻辑、两个标准 include 和固定写点。页面授权仍只有 gui.controls；Agent 仍由可信原生已持有进程选择。任何页面输入都不能打开观察、建立 grant 或覆盖字段。字段包含原生关联标识，应只留在 Root 的私有验证输出，不写进页面、管道响应或公共验收记录。无 SDK ABI/API 改动。

采纳前检查 live main.cpp whole SHA 等于 delta.json before pin，并逐跨度应用，不覆盖别人的修改。仅本候选无法证明完整运行时来源、跨账户接入、任务跨导航存活、8 小时稳定性、键盘/IME或帧呈现。原固定日志 smoke 可先执行，但必须将数字 session/receipt/digest 绑定标为未观察。

## 验证计划

Root 先全文读取 17 个（原始 14 个加撤出修正 3 个）精确跨度和 whole reverse，再正常 SDK25 编译三个真实应用。验证 opt-in 未启用时不产生新观察文件；启用时按每个实际独立 Owned Job 的 PID/FILETIME 匹配记录，固定阶段无重复且 QPC 不逆行、native.end exitCode 与 root exit 一致。Compat 要求非零十进制 contextReceipt、Modern 要求真实非空 digest，另一字段为 null；只把 bind 那一时点的记录作为当时的实际绑定关联，不能授权后来动作。缺身份、错 FILETIME、跨应用/跨窗口、重复 bind、乱序 teardown、自然失败或强制关闭均应拒绝。
