# 已知限制与生产门槛

本交付解决分层、接入、自行构建和验证的问题，仍为预览版本。

[原生调度快照](../verification/production-validation/preview11-renewal-scheduler-stack-2026-09-26/README.md)确认 x86 低优先级解析任务相对上一帧时钟迟约 10.28 秒。[策略回归](../verification/production-validation/scheduler-fairness-policy-2026-09-26/README.md)先失败、后两架构各十二项通过；[x86](../verification/production-validation/scheduler-fairness-x86-rebuild-2026-09-26/README.md)和 [x64](../verification/production-validation/scheduler-fairness-x64-rebuild-2026-09-27/README.md)完整重编、保留原动画的短测均退出 0，公共任务、取消、退订和关闭完成。冻结消费者只替换内核，实际消除了这两个启动复现；新内核已纳入 preview.15 候选，长期资源和生产门槛仍待验收。

当前 preview.15 已补齐[原仓库双架构 14 次桌面短测](../verification/production-validation/preview15-desktop-2026-09-27/README.md)、[完整源码无 Git 解压四配置构建](../verification/production-validation/preview15-full-source-build-2026-09-27/README.md)及[新编 EXE 六入口短测](../verification/production-validation/preview15-relocated-desktop-2026-09-27/README.md)。[Win7 SP1 x86 当前三入口](../verification/production-validation/win7-sp1-x86-qemu-preview15-apps-attempt3-2026-09-27/README.md)也在原 60 秒期限内原生退出 0，完整关闭、客体身份和运行前后资源/legal 通过，未观察到外部负载。旧 preview.11 关闭停滞未在本次当前输入重现；V02 真实交互、重复生命周期、真实续期、隔离长稳、干净机器及物理目标机仍未通过。以下历史失败按各自输入保留。

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
| 资源部署改名失败 | 本轮 x86 构建有一次 `EPERM`；旧资源恢复、未改源码重试及后续启动通过，失败日志保留，具体占用原因未确认 |
| SDK 包阶段目录改名失败 | preview.9 与 preview.10 首次源包发布目录改名均出现 `EPERM`；未改源码的新目标重试和文件核验通过。保留 [preview.9](../verification/production-validation/preview9-public-tray-preflight-2026-09-26/README.md) 与 [preview.10](../verification/production-validation/preview10-residency-preflight-2026-09-26/README.md) 原失败；日志缺瞬时锁持有者与逐次原生错误，根因未确定，重试成功不关闭缺陷 |
| 完整源码搬移重编 | preview.6 无 Git 源包在 CP936 下重编 x86 Miniblink 曾因 UTF-8 误读失败；preview.7 定点 BOM 修复后重编通过，但旧 SOUI 工程在源码树生成 112 个额外产物。[preview.8 候选源码包](../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)将 SOUI 构建隔离到 `out/`，在两个全新无 Git 解压目录完成 x86/x64 全量底层重编、四组应用构建与测试、重编 DLL 启动，且编译后完整清单仍分别复验通过；[Win7 SP1 x86 QEMU](../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)三入口短测通过。[独立业务小样升级回退](../verification/production-validation/preview8-independent-upgrade-2026-09-25/README.md)双架构通过。最终打包、目标物理设备与完整交付门仍未闭合，`sourceDeliveryVerified=false` |
| x86 公共连接器 ABI | 已修正交付动态绑定头中 54 个 `bool`/`BOOL` 返回类型不一致；preview.4 x86 软件路径四模式各 1,000 轮正常退出。[preview.5 Win7 SP1 x86 QEMU 公共消费者四模式](../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)各 3 轮通过，[三应用入口](../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)短测通过；[新诊断源码的 100 次重载](../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)也退出 0。preview.5 宿主 x86 Bridge 千次循环两次超时，完整资源门槛、物理目标机和业务链仍缺失 |
| preview.5 x86 Bridge 长循环超时 | [独立公共消费者复测](../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)的目标 1,000 轮两次分别在第 86、977 轮挂载阶段由监督器终止；同载荷另三次诊断跑满。两次失败与其他 QEMU 负载重叠，但自有 QEMU 快照并行的诊断也跑满，不能据此认定因果。[现场诊断](../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)已在超时路径加入监督器日志偏移与可选 x86 原生抓栈；新源码两次千轮通过，原源码重新编译的另一个二进制载荷也一次千轮通过，但都未触发故障抓取。根因未确定，此项阻止生产准入 |
| 反复销毁重建后的资源累积 | 已修补 ToolTip、拖拽 HRGN、线程唤醒事件三处遗漏；preview.4 固定包两架构软件路径重载、销毁重建和三窗口各 1,000 轮正常退出。内存/句柄有回收波动，两架构三窗口分段内存中位数总体上升；资源所有权、GPU 和 8 小时门槛未通过，见[生命周期记录](acceptance/lifecycle.md) |
| 公共任务表背压 | preview.5 在 1,024 条任务全未结束时拒绝新任务，文件扫描与 Workbench 返回 `E_BUSY`；x86/x64 原生边界、并发、取消及扫描错误传播测试通过。仍需直接公共服务消费、真实排队超时、关窗竞态及 Win7 上此版本的完整业务验证，不能把这一局部修复当作公共任务能力整体通过 |
| Electron 流畅性对标 | 尚无完整同负载、实机输入、长期帧延迟的等价验收，不能宣称追平 |
| GPU compositor 生命周期 | 旧默认 GPU 应用曾在 `wkeDestroyWebView` 阶段以 `0xC0000005` 退出；当前显式 GPU 短测未复现该退出，但独立消费者双架构 30 轮重建的 USER 对象每轮约增 1，Runtime 关闭后仍残留。普通句柄的短期上升在软件路径也出现并有后续回收，不能混为同一个缺陷。合成线程释放与 Skia 接口引用修补降低了内存末点，仍未关闭 GPU 资源门槛，见[专项记录](acceptance/gpu-lifecycle.md)。模板和 Workbench 默认软件路径；GPU 模式仅为实验 |
| 目标系统 | 开发机通过不代表 Windows 7/10/11 全矩阵；各系统与补丁级别独立验证 |
| V02 低分辨率窗口 | Workbench 的 `--height` 当前至少取 640，而 Win7 x86 客体预检桌面为 800×600；这组原始显示条件下完整可见页面尚未验收。[第一次可见探索](../verification/production-validation/win7-sp1-x86-v02-display-failure-2026-09-28/README.md)在第 10 秒检查过早；[第二次探索](../verification/production-validation/win7-sp1-x86-v02-visible-page-2026-09-28/README.md)在临时 1024×768 下第 35 秒探测到窗口并拍到 V02 数据页，应用原生退出 0、正常关闭。[第一轮滚轮探索](../verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28/README.md)发送八次原生滚轮但缺滚前截图；[新一轮前后截图](../verification/production-validation/win7-sp1-x86-v02-layout-wheel-before-after-2026-09-28/README.md)显示列表顶部移动到下方公共任务区，并有十二次自然前台原生滚轮原件。中文 IME 组合、缩放及公共任务并行未执行；原 800×600 条件和完整 V02 交互仍未放行。 |
| DPI、多屏、输入法、辅助功能 | 需要实际产品和设备回归 |
| 浏览器能力 | Miniblink49 分支；页面按 Chrome 49 构建，不能假定现代 Web API 可用 |
| 不可信网页 | 模板面向本地受控页面；远程不可信内容需专项评估内核与原生能力边界 |
| 内核网络及安全维护 | preview.4 固定 DLL 已换入双架构重编译候选并再次完成回环 HTTPS 正负向测试；系统信任根、代理、动态子资源、逐组件补丁覆盖及维护责任仍未闭环，见[核对记录](acceptance/kernel-maintenance.md) |
| API / ABI | preview 接口可能迁移，不保证跨编译器 C++ ABI |
| 公共驻留能力 | preview.10 在托盘基础上增加公共菜单、全局快捷键及显式 Workbench 三窗口驻留。preview.11 [真实 x86 短测](../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)中，托盘消费者在菜单显示前返回 `E_MENU_FOREGROUND`、退出 1；Workbench 驻留 slot 2 达到内部 45 秒就绪期限、退出 3，尚未进入 smoke 阶段，slot 3 未判定。两项清理完成，根因尚未确证；x64 菜单/驻留、实际菜单/快捷键输入、Explorer 重启、三窗任务隔离、模态关闭、空窗驻留及 Win7 x86 仍未执行，V04 不通过。此前 preview.9 短测不覆盖这些新增行为 |
| 同文档长会话续接 | preview.11 已实现显式公共续期、稳定授权资源归属与 Web 自动续期，[原生契约及安装后独立消费者](../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)在两架构通过，设计见 [ADR-0014](adr/0014-same-document-session-renewal.md)。[真实时钟续期独立消费者](../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)的 x86/x64 编译与无窗口自检通过；随后[本机 x86 GUI 首次短测](../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)在页面就绪前因会话关闭退出 1，任务未启动，x64 GUI 与真实超过 30 分钟/八小时运行未执行。[v2 修正夹具](../verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/README.md)已完成两架构编译、每架构 45 项无窗口阶段自检、106 项分析器合成检查及 8 项运行输入绑定检查；随后[两架构首次 GUI](../verification/production-validation/preview11-real-renewal-v2-smoke-2026-09-26/README.md)均因初始 60 秒未就绪退出 1，bootstrap 和任务调用为 0，真实续期未通过；模拟时钟和短测不能替代任务/订阅跨轮换保留的真实长时验收。长模态或休眠跨过期限仍明确失败，完整门槛未关闭 |
| 依赖授权 | 已保留来源、散列及授权文本；精确二进制再分发审核尚未完成 |
| Web 构建依赖审计 | Vite 6.4.3 / PostCSS 8.5.23 兼容性升级后，本次 npm audit：SDK、模板、Demo 均为 0 项；完整结果在 verification/*-audit.json。后续更新继续由锁文件、Dependabot 和发布验收共同约束 |
| 自动验证 | 按当前维护决策不配置远程 CI；每个 PR 必须提供适用的本机构建、测试和真实桌面验收证据 |
| 分支保护 | 当前私有仓库方案不支持 GitHub 分支保护；维护者通过 PR、CODEOWNERS、Squash 合并和人工评审执行流程约束 |
| 安装、更新、签名 | 由实际项目配置，并验证升级和回退 |

发布负责人应针对具体产品逐项补证据、处理未关闭缺陷，再确定生产版本。验收结果保持 `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
