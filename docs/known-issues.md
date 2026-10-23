# 已知限制与生产门槛

2026-10-23 新增 [ACK 反馈修复候选](maintenance/scroll-ack-feedback-20261023.md)：普通合成滚动的整数回传可能误增 epoch，撤销已接纳目标。候选明确区分 ACK 与外部意图，保留真实边界；默认 fractional=false 的源码静态复核及隔离原生回归已完成：当前两架构各 25+2+19+13 项通过，旧对照各 3/10 并退出 1。完整 x86 内核已构建并获独立 build-only 验收；GUI21 的 800 请求位移误差小于 1e-6，GUI22 同值 CSS 赋值后新 epoch 保持停止。结论限定默认整数元素滚动样本，整体流畅度、完整 x64 内核与发布资格另验。[ADR 0120](adr/0120-preserve-compositor-scroll-acknowledgements.md)保持 proposed，不修改下列历史结果或固定二进制。

2026-10-23 生产 Controller/Snapshot 回归确认：相同且均已接收的 `+250`、`+100` 输入，因临时光栅覆盖到达时序不同而分别停在 350、300。新的源码候选分离逻辑目标与可绘制边界，暂停时不空转，ready 场景激活后再汇总下一帧需求；两架构的两场景原始回归、十九项边界和既有十三项覆盖回归均已通过。完整内核、真实 producer 唤醒和 GUI 流畅度仍待单独验证，见 [ADR-0119](adr/0119-preserve-scroll-target-beyond-raster-coverage.md)及[维护记录](maintenance/scroll-target-retention-20261023.md)。另一个原有的同 epoch 覆盖收缩/再扩展反例在旧版和首修复版、两架构均真实失败：显示位置从 200 回放到 250；该问题保持未解决，不被上述局部通过覆盖。固定 0.5.0 二进制和发布资格不变。

2026-10-23 发现独立滚动后，鼠标按下位置没有可捕获的滚动目标时，路由器仍把旧的可视采样或失败捕获的部分输出作为新屏障交给引擎；屏障同步拒绝会丢弃该次按下。当前源码只在本次捕获成功时附带屏障，失败时保留无屏障的正常按下，并增加 [十二项生产路由器回归](testing.md)。成功屏障仍须同步，抬起仍遵循原有引擎 FIFO。固定 0.5.0 SDK 尚未包含本修复；真实滚动、标题拖动、性能、两架构和最终交付仍需分别验证。

2026-10-21 隔离消费者中复现了输入进入 SDK 后超过 100 ms 的排队，并与兼容内核约 50 ms 的定时消息回调重叠。当前源码候选限制正常主线程连续调度批次，见 [ADR-0118](adr/0118-main-thread-timer-batches.md)。固定 0.5.0 二进制尚未包含此候选；单个长回调、任务接纳开销、保留的 16 ms 定时等待及持续负载吞吐仍需分别核实，不能把局部批次回归视为全部输入延迟或生产性能验收。

后续[两架构重编与实际短测](../verification/production-validation/input-latency-scheduling-2026-10-21/README.md)记录了输入队列长等待减少、刷新/关闭完成，也保留了有限页面消息突发变慢及主程序仍有单次长原生回调。单次固定顺序、窗口遮挡和少量点击的记录不足以完成正式性能验收；源码候选尚未导入固定 SDK 或产品交付。
2026-10-16 [Issue 专项核验](../verification/production-validation/issue-maintenance-2026-10-16/README.md)补齐 #76 的便携默认 ALL 与私有测试注册边界证据。另对发布散列匹配的六个 0.5.0 应用 ZIP 核对每包 198 份声明材料，均与保留源码原字节一致，包含 OpenSSL/SSLeay 原文；下文 preview.15 的缺件记录保留历史含义。当前链接成员清单、适用条款审批及首方许可决定仍未完成，#39 和生产门槛继续开放。

2026-10-16 一次原生 UIA `mutations` 复测在焦点请求处失败，旧日志未保留 HRESULT；随后 120 次定向诊断未复现，不能据此判定已修复。当前测试已补充失败时的错误码、实际焦点和阶段时间，断言与等待上限不变。系统随后重启并升级，受影响工具与最终候选验证重新执行；旧失败保持原始环境归属。

现代模板现显式启用会话续租。0.5 主线已分离普通请求、清理和专用续期容量，并完成页面策略与原生回归；此前业务填满传输导致续期容量错误的复现保留为历史证据。真实宿主双层满载、跨续期任务保留和八小时持续压力仍未验收，见 [0.5 执行清单](roadmap/0.5.0-execution.md)。

0.5 主线已修正无 Git 校验器跳过清单内历史 `out/` 文件的问题，15 项回归及仓库外搬移源码后的双架构 SDK-only 重建通过。历史原件字节与路径保留；最终集成候选的完整应用、离线重建与搬移验证仍需重新执行。

本交付解决分层、接入、自行构建和验证的问题。本次预发布交付身份为 `0.5.0`，生产验收保持 `releaseReady=false`；本轮实际范围和保留失败见[发布说明](releases/0.5.0.md)，`0.4.0-preview.1` 为历史发布。以下带版本的证据均只适用于各自冻结载荷，完整交付与生产门槛不继承。

0.4 预览版将 WebView2 适配器纳入可选公共组件，并提供独立现代模板；下文 0.3 的实验记录保留历史含义。公共组件可用不等于全环境认证。Win7/PE 规划采用兼容路径；现代档要求支持的桌面系统与实际 WebView2 Runtime。0.4.0-preview.1 的 Win7 SP1 x86 短启动通过，但固定兼容内核在 Setup PE 3.1 x86 中因 OPENGL32.dll 静态依赖失败。另建 x86 无 GPU 候选在该 PE 完成就绪、关闭并退出 0；其结果不覆盖固定 SDK 内核。高刷性能、GPU 资源、8 小时稳定性、输入法及 PE 镜像矩阵仍按 [0.4 执行清单](roadmap/0.4.0.md) 验收。

0.4.0-preview.1 的 x64 兼容/现代后端各完成 1,000 轮重建并正常退出；兼容后端分段内存中位数仍上升，不能宣布资源已收敛。该历史发布的工程交付汇总 `sourceDeliveryVerified=true`，生产准入 `releaseReady=false`；表中历史版本的门槛状态不覆盖当前整合版本。证据见 [0.4 记录](../verification/production-validation/preview04-2026-10-08/README.md)。

preview.10 新增受权动作schema、文档能力白名单和受控页面界面语义；它们不等同于外部Agent主体、UIA或全平台GUI。WebView2仍为独立实验。2026-10-03审计发现Tailwind依赖链的braces无已发布补丁，已移除该构建链并保留现有静态工具样式与MIT文本；三处npm审计以本版重新执行结果为准。长期资源增长和性能差距不会因公开预发布自动关闭。

[原生调度快照](../verification/production-validation/preview11-renewal-scheduler-stack-2026-09-26/README.md)确认 x86 低优先级解析任务相对上一帧时钟迟约 10.28 秒。[策略回归](../verification/production-validation/scheduler-fairness-policy-2026-09-26/README.md)先失败、后两架构各十二项通过；[x86](../verification/production-validation/scheduler-fairness-x86-rebuild-2026-09-26/README.md)和 [x64](../verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27/README.md)完整重编、保留原动画的短测均退出 0，公共任务、取消、退订和关闭完成。冻结消费者只替换内核，实际消除了这两个启动复现；新内核已纳入 preview.15 候选，长期资源和生产门槛仍待验收。

历史 preview.15 已补齐[原仓库双架构 14 次桌面短测](../verification/production-validation/preview15-desktop-2026-09-27/README.md)、[完整源码无 Git 解压四配置构建](../verification/production-validation/preview15-full-source-build-2026-09-27/README.md)及[新编 EXE 六入口短测](../verification/production-validation/preview15-relocated-desktop-2026-09-27/README.md)。[Win7 SP1 x86 当前三入口](../verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt3-2026-09-27/README.md)也在原 60 秒期限内原生退出 0，完整关闭、客体身份和运行前后资源/legal 通过，未观察到外部负载。旧 preview.11 关闭停滞未在本次当前输入重现；V02 真实交互、重复生命周期、真实续期、隔离长稳、干净机器及物理目标机仍未通过。以下历史失败按各自输入保留。

2026-09-28 V02 的 [Win7 v6 第二次交互](../verification/production-validation/win7-sp1-x86-v02-interaction-v6-attempt2-interrupted-2026-09-28/README.md)在真实桌面发出一次冻结命令后遭外部 QEMU 打断；我方门禁仅停自身，驱动退出 1、客体无串口导出。关键帧可见页面和仍为 `idle` 的任务区，但缺完整客体结果，不能判断任务、中文 IME 或缩放验收，也不能将此次作为隔离性能样本。旧短测不覆盖此场景；后续须先复核客户区点击坐标，另用新 attempt 完成客体验收。

新 [Win7 v7 交互尝试](../verification/production-validation/win7-sp1-x86-v02-interaction-v7-resize-blocked-2026-09-28/README.md)在无外部 QEMU 干扰时完成 12 次原生外层滚轮，但 Workbench 默认窗口右下边框落在 1024×768 桌面的任务栏下；真实缩放探针拒绝被覆盖的边框，客体退出 1。任务、中文 IME、内层滚动及完整关闭检查未执行，V02 仍不通过。当前问题按 [Issue #42](https://github.com/chenziyang110/soui-web-sdk/issues/42) 继续处理，不得把驱动/QEMU 正常退出当成应用交互通过。

[Win7 v8 输入](../verification/production-validation/win7-sp1-x86-v02-v8-prelaunch-refusal-2026-09-28/README.md)新增原生标题栏拖动后再缩放，x86 探针编译和冻结检查通过；客体第一次尝试在自身 QEMU 启动前遇到外部 VM，保护性退出 1，未产生交互结果。v8 的固定任务点击位置也未确认，后续即使结构检查通过，仍需用页面状态、进度事件和查询原件证明任务启动及并行，并单独证明真实 IME 组合。默认窗口位置的产品修复位于本分支，已完成现代 Windows x86/x64 正式构建及原生缩放短测；Win7 仍未验收，Issue #42 保持开放。

[Win7 v8 第二次客体](../verification/production-validation/win7-sp1-x86-v02-v8-command-interrupted-2026-09-28/README.md)确认真实桌面后向 QEMU 发送一次冻结命令按键，但串口 0 字节且没有客体原生结果；外部 QEMU 随后出现，门禁只停我方客体。驱动退出 1、我方 QEMU 退出 -1。不能把按键已发送解释为应用已运行，也不能据此关闭 v7 窗口问题或中文 IME/任务/缩放门槛。下一轮使用独立 attempt 和修复后的新构建；修复版已完成现代 Windows x86/x64 原生缩放短测，Win7 V11 客体验收仍待执行。

[SDK Core 体积修正](../verification/production-validation/sdk-core-native-backups-2026-09-27/README.md)排除两份历史内核备份，并保留完整源码原件。内部 preview.14 的源码匹配失败后，preview.15 统一打包和校验筛选，真实新包匹配及安装往返通过，净减少约 55.94 MiB；不据此扩大应用运行体积或 Electron 对标结论。

冻结 preview.11 的[页面动画诊断](../verification/production-validation/preview11-renewal-animation-diagnostic-2026-09-26/README.md)中，相同输入只关闭旋转动画即可完成任务短测；动画开启时解析持续停滞，尚需原生调度证据。另一已就绪运行在页面记录后 15 ms 被监督器判定为“30 秒无记录”。[新外部夹具](../verification/production-validation/preview11-renewal-supervisor-clock-2026-09-26/README.md)已复现并修正先采样时钟、后读跨线程快照导致的无符号差值风险；两架构边界测试、自检及关闭动画的诊断短测通过。原失败保留，关闭动画不作为 SDK 修复，真实续期和生产门槛继续未通过。

preview.13 [完整源码解压身份检查](../verification/production-validation/preview13-legal-identity-2026-09-26/README.md)发现一份许可证的 Git 换行转换，使旧源码 ZIP 与同版 419 文件 SDK 包不一致。Git 规则和回归已修正，旧包/失败目录保留。[新完整源码包](../verification/production-validation/preview13-full-source-build-2026-09-26/README.md)已在无 Git 解压目录匹配同版 SDK，完成 SDK/Starter/Workbench 四配置构建、44 项原生及 8 项交付身份测试；新载荷 GUI、底层完整重编及干净机器复现仍待执行。验证输入是 `a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68`；SDK 实现与既有安装包字节不变。

当前 preview.13 的[新事务协议](../verification/production-validation/preview13-install-recovery-2026-09-26/README.md)已通过真实进程终止和完整 SDK 包文件恢复测试；[已知旧现场的独立副本](../verification/production-validation/preview13-legacy-recovery-build-2026-09-26/README.md)已手工恢复并升级，原件未变，独立账本 x86/x64 Debug/Release 编译通过。旧无 journal 自动恢复仍拒绝，文件/目录形态迁移明确拒绝；本候选业务与应用 GUI、受限账户及目标平台尚未重验。以下 preview.11 失败仍按其原输入保留，不能由安装器修复视为全部关闭。

preview.11 [真实安装中断](../verification/production-validation/preview11-upgrade-interruption-2026-09-26/README.md)暴露恢复缺陷：安装进程在旧文件已移入备份后退出，正式校验与重新安装均失败。旧 SDK 字节和业务数据未丢失，当时 CLI 无法自动恢复。preview.13 已补齐新协议的持久事务、恢复入口与冲突处理；旧现场仅有上述确切副本的手工恢复证据，不能扩展为通用旧格式恢复。

preview.11 Win7 SP1 x86 [第三轮客体](../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt3-2026-09-26/README.md)已通过传输与运行前文件身份检查，Starter 就绪并退出 0；Workbench 默认入口在就绪后关闭超时，React 和运行后载荷校验未执行。三入口整体仍失败，前两轮传输失败保留。

2026-09-26 preview.11 [本机桌面记录](../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)已补足 x86/x64 四项原生桌面探针、Starter/Workbench 默认与 React 六入口及独立生成项目两项短启动，均取得退出码 0。x86 公共菜单前台激活与 Workbench 驻留就绪仍有真实失败；这些短测不关闭完整 V02/V04、Win7、真实长时续期或资源/性能门槛。

| 项目 | 当前状态 / 必须补齐 |
|---|---|
| x64 重载内存与关闭稳定性 | 独立消费者已复现并修复逐窗口初始化导致的重建崩溃/多窗口挂起；100 次重载操作通过，资源和长期稳定门槛仍未关闭，见 [本轮记录](acceptance/lifecycle.md) |
| 资源部署改名失败 | 0.5.0 发布构建中两次完整 x64 Release 重现 `EPERM`；现沿用 SDK 安装器的有界 Windows 重试，相关回归与修复后的两架构四配置构建通过。原失败保留，具体占用方仍未确定，持续拒绝仍报错并恢复旧资源 |
| SDK 包阶段目录改名失败 | preview.9 与 preview.10 首次源包发布目录改名均出现 `EPERM`；未改源码的新目标重试和文件核验通过。保留 [preview.9](../verification/production-validation/preview9-public-tray-preflight-2026-09-26/README.md) 与 [preview.10](../verification/production-validation/preview10-residency-preflight-2026-09-26/README.md) 原失败；日志缺瞬时锁持有者与逐次原生错误，根因未确定，重试成功不关闭缺陷 |
| 完整源码搬移重编 | preview.6 无 Git 源包在 CP936 下重编 x86 Miniblink 曾因 UTF-8 误读失败；preview.7 定点 BOM 修复后重编通过，但旧 SOUI 工程在源码树生成 112 个额外产物。[preview.8 候选源码包](../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)将 SOUI 构建隔离到 `out/`，在两个全新无 Git 解压目录完成 x86/x64 全量底层重编、四组应用构建与测试、重编 DLL 启动，且编译后完整清单仍分别复验通过；[Win7 SP1 x86 QEMU](../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)三入口短测通过。[独立业务小样升级回退](../verification/production-validation/preview8-independent-upgrade-2026-09-25/README.md)双架构通过。最终打包、目标物理设备与完整交付门仍未闭合，`sourceDeliveryVerified=false` |
| x86 公共连接器 ABI | 已修正交付动态绑定头中 54 个 `bool`/`BOOL` 返回类型不一致；preview.4 x86 软件路径四模式各 1,000 轮正常退出。[preview.5 Win7 SP1 x86 QEMU 公共消费者四模式](../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)各 3 轮通过，[三应用入口](../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)短测通过；[新诊断源码的 100 次重载](../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)也退出 0。preview.5 宿主 x86 Bridge 千次循环两次超时，完整资源门槛、物理目标机和业务链仍缺失 |
| preview.5 x86 Bridge 长循环超时 | [独立公共消费者复测](../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)的目标 1,000 轮两次分别在第 86、977 轮挂载阶段由监督器终止；同载荷另三次诊断跑满。两次失败与其他 QEMU 负载重叠，但自有 QEMU 快照并行的诊断也跑满，不能据此认定因果。[现场诊断](../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)已在超时路径加入监督器日志偏移与可选 x86 原生抓栈；新源码两次千轮通过，原源码重新编译的另一个二进制载荷也一次千轮通过，但都未触发故障抓取。根因未确定，此项阻止生产准入 |
| 反复销毁重建后的资源累积 | 已修补 ToolTip、拖拽 HRGN、线程唤醒事件三处遗漏；preview.4 固定包两架构软件路径重载、销毁重建和三窗口各 1,000 轮正常退出。内存/句柄有回收波动，两架构三窗口分段内存中位数总体上升；资源所有权、GPU 和 8 小时门槛未通过，见[生命周期记录](acceptance/lifecycle.md) |
| 公共任务表背压 | preview.5 在 1,024 条任务全未结束时拒绝新任务，文件扫描与 Workbench 返回 `E_BUSY`；x86/x64 原生边界、并发、取消及扫描错误传播测试通过。仍需直接公共服务消费、真实排队超时、关窗竞态及 Win7 上此版本的完整业务验证，不能把这一局部修复当作公共任务能力整体通过 |
| Electron 流畅性对标 | 尚无完整同负载、实机输入、长期帧延迟的等价验收，不能宣称追平 |
| 基准失败清理 | [失败清理回归](../verification/production-validation/benchmark-safe-stop-2026-09-29/README.md)已将两级监督器限制为只停止持有的直接子进程；Windows 父 PID 可能复用，不能仅凭直接子进程身份安全终止整棵树。失败后的后代清理仍未证实，正式下一轮前必须核查残留；尚无可靠的身份绑定整树清理机制，也没有隔离的五对性能结论。 |
| GPU compositor 生命周期 | 旧默认 GPU 应用曾在 `wkeDestroyWebView` 阶段以 `0xC0000005` 退出；当前显式 GPU 短测未复现该退出，但独立消费者双架构 30 轮重建的 USER 对象每轮约增 1，Runtime 关闭后仍残留。普通句柄的短期上升在软件路径也出现并有后续回收，不能混为同一个缺陷。合成线程释放与 Skia 接口引用修补降低了内存末点，仍未关闭 GPU 资源门槛，见[专项记录](acceptance/gpu-lifecycle.md)。模板和 Workbench 默认软件路径；GPU 模式仅为实验 |
| 目标系统 | 开发机通过不代表 Windows 7/10/11 全矩阵；各系统与补丁级别独立验证 |
| V02 低分辨率窗口 | 旧候选 Workbench 的 `--height` 至少取 640，而 Win7 x86 客体预检桌面为 800×600；这组原始显示条件下完整可见页面尚未验收。[第一次可见探索](../verification/production-validation/win7-sp1-x86-v02-display-failure-2026-09-28/README.md)在第 10 秒检查过早；[第二次探索](../verification/production-validation/win7-sp1-x86-v02-visible-page-2026-09-28/README.md)在临时 1024×768 下第 35 秒探测到窗口并拍到 V02 数据页，应用原生退出 0、正常关闭。[第一轮滚轮探索](../verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28/README.md)发送八次原生滚轮但缺滚前截图；[新一轮前后截图](../verification/production-validation/win7-sp1-x86-v02-layout-wheel-before-after-2026-09-28/README.md)显示列表顶部移动到下方公共任务区，并有十二次自然前台原生滚轮原件。[V7 真实缩放失败](../verification/production-validation/win7-sp1-x86-v02-interaction-v7-resize-blocked-2026-09-28/README.md)表明旧候选窗口下边框被任务栏覆盖。本分支已完成两架构正式 Release 构建、无窗口几何回归，以及[现代 Windows x86/x64 原生 80×60 缩放和关闭实测](../verification/production-validation/work-area-placement-gui-2026-09-28/README.md)；Win7 的新冻结输入与真实窗口边框仍待验证，小于 850×640 的工作区也尚未证明可完成 80×60 缩放。中文 IME 组合、Win7 缩放及公共任务并行未通过；原 800×600 条件和完整 V02 交互仍未放行。 |
| DPI、多屏、输入法、辅助功能 | 需要实际产品和设备回归 |
| 浏览器能力 | Miniblink49 分支；页面按 Chrome 49 构建，不能假定现代 Web API 可用 |
| 不可信网页 | 模板面向本地受控页面；远程不可信内容需专项评估内核与原生能力边界 |
| 内核网络及安全维护 | preview.4 固定 DLL 已换入双架构重编译候选并再次完成回环 HTTPS 正负向测试；系统信任根、代理、动态子资源、逐组件补丁覆盖及维护责任仍未闭环，见[核对记录](acceptance/kernel-maintenance.md) |
| API / ABI | preview 接口可能迁移，不保证跨编译器 C++ ABI |
| 公共驻留能力 | preview.10 在托盘基础上增加公共菜单、全局快捷键及显式 Workbench 三窗口驻留。preview.11 [真实 x86 短测](../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)中，托盘消费者在菜单显示前返回 `E_MENU_FOREGROUND`、退出 1；Workbench 驻留 slot 2 达到内部 45 秒就绪期限、退出 3，尚未进入 smoke 阶段，slot 3 未判定。两项清理完成，根因尚未确证；x64 菜单/驻留、实际菜单/快捷键输入、Explorer 重启、三窗任务隔离、模态关闭、空窗驻留及 Win7 x86 仍未执行，V04 不通过。此前 preview.9 短测不覆盖这些新增行为 |
| 同文档长会话续接 | preview.11 已实现显式公共续期、稳定授权资源归属与 Web 自动续期，[原生契约及安装后独立消费者](../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)在两架构通过，设计见 [ADR-0014](adr/0014-same-document-session-renewal.md)。[真实时钟续期独立消费者](../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)的 x86/x64 编译与无窗口自检通过；随后[本机 x86 GUI 首次短测](../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)在页面就绪前因会话关闭退出 1，任务未启动，x64 GUI 与真实超过 30 分钟/八小时运行未执行。[v2 修正夹具](../verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/README.md)已完成两架构编译、每架构 45 项无窗口阶段自检、106 项分析器合成检查及 8 项运行输入绑定检查；随后[两架构首次 GUI](../verification/production-validation/preview11-real-renewal-v2-smoke-2026-09-26/README.md)均因初始 60 秒未就绪退出 1，bootstrap 和任务调用为 0，真实续期未通过；模拟时钟和短测不能替代任务/订阅跨轮换保留的真实长时验收。长模态或休眠跨过期限仍明确失败，完整门槛未关闭 |
| 依赖授权 | 已保留部分来源、散列及授权文本；[历史 preview.15 运行包核对](../verification/production-validation/license-runtime-notices-2026-09-28/README.md)确认 x86 链接命令显式列出 `openssl.lib`，[同一构建的链接读取补充](../verification/production-validation/license-link-read-traces-2026-09-28/README.md)显示 x86/x64 链接器均打开该库。[匹配 x86 DLL/PDB 的成员记录](../verification/production-validation/license-pdb-members-2026-09-29/README.md)进一步列出 501 个来自 `openssl.lib` 的编译单元；精确 x64 DLL 没有 CodeView、采集时无链接 PDB，尚无同等级证据。该历史版本四份 Starter/Workbench 随包 `resources/legal/` 均未包含导入的 OpenSSL/SSLeay 许可文本。[OpenSSL 官方说明](https://openssl-library.org/source/license/)确认 1.0.2h 适用旧双许可。适用再分发条件与首方许可仍待审阅，见 [Issue #39](https://github.com/chenziyang110/soui-web-sdk/issues/39)；`releaseReady=false` |
| Web 构建依赖审计 | Vite 6.4.3 / PostCSS 8.5.23 兼容性升级后，本次 npm audit：SDK、模板、Demo 均为 0 项；完整结果在 verification/*-audit.json。后续更新继续由锁文件、Dependabot 和发布验收共同约束 |
| 自动验证 | 按当前维护决策不配置远程 CI；每个 PR 必须提供适用的本机构建、测试和真实桌面验收证据 |
| 分支保护 | 当前私有仓库方案不支持 GitHub 分支保护；维护者通过 PR、CODEOWNERS、Squash 合并和人工评审执行流程约束 |
| 安装、更新、签名 | 由实际项目配置，并验证升级和回退 |

发布负责人应针对具体产品逐项补证据、处理未关闭缺陷，再确定生产版本。验收结果保持 `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

[V9 当前 Workbench Win7 SP1 x86 可见交互探索](../verification/production-validation/win7-sp1-x86-v02-workarea-v9-failure-2026-09-29/README.md)在新冻结输入和 1024×768 桌面下，九次窗口探测仍未找到可见顶层窗口，客体整轮退出 1。应用由测试脚本发出关闭请求后原生退出 0，不能证明可见页面或工作区定位修正。需在新输入中诊断窗口可见性和启动时序；缩放、滚动、任务和中文输入仍开放。

[V10 只读时序诊断](../verification/production-validation/win7-sp1-x86-v02-window-visibility-v10-2026-09-29/README.md)在同一官方 x86 构建及 Win7 SP1 x86 快照中，10–40 秒的七次窗口探针未检出窗口，客体单调计时 46.933 秒首次检出，之后至 105 秒保持可见。该轮应用和客体均原生退出 0，但关闭由夹具请求，未执行缩放、滚动、真实中文 IME 或公共任务并行；V9 的真实失败和 Issue 42 均保持开放。尚不能判断可见性波动原因或放行 Win7 工作区定位。
