# 0.3 SDK 中期准入判定

初始判定日期：2026-09-24，候选 `0.3.0-preview.5`；后续增补按各自版本分别判定。本文件只汇总已执行证据，不是正式发布批准。完整门槛见[验证方案](../roadmap/0.3-sdk-validation.md)，逐能力缺口见[能力矩阵](capabilities.md)，原始结果按 run-id 保存在 `verification/production-validation/`。当前 `releaseReady: false`。

## 2026-09-26 preview.11 预检与桌面短测

**双架构 Release 编译、安装消费、无 GUI 契约与所列桌面短测通过；驻留切片存在真实失败，指定范围生产未通过；相对 Electron 的整体采用价值未判定。** [本轮预检归档](../../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)记录 SDK、Starter、Workbench 的 x86/x64 完整编译、独立 SDK 安装和仓库外 `session-consumer` 编译/运行均退出 0；每架构 10 项 CTest 通过，其中原生授权续期为 14 组契约。最终汇总确认 Web 62 项、包行为 14 项、交付身份 7 项通过，五份锁文件的全依赖审计漏洞总数均为 0；418 文件 SDK 首次打包、包内散列与源码匹配通过。生成的仓库外 `RenewalStarter` 双架构 Release 编译与项目校验均退出 0。

[后续桌面归档](../../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)沿用相同编译输入：本机 x86/x64 的窗口线程停滞、目录程序化取消共四项，Starter/Workbench 默认与 React 共六入口，以及 `RenewalStarter` 两项真实短启动均退出 0。生成项目直接监督器仅对 EXE 做运行前后采样；另有完整载荷复核，不能当作每轮退出时逐文件采样或文件锁定。首次缺退出码的窗口探针和错误生成项目脚本入口均保留为失败样本，不由后续成功覆盖。

x86 托盘消费者在菜单显示前返回 `E_MENU_FOREGROUND`、退出 1；x86 Workbench 驻留的 slot 2 达到内部 45 秒就绪期限、退出 3，尚未进入 smoke 阶段，slot 3 状态未判定。两项均完成清理；x64 菜单/驻留、实际菜单/快捷键输入、Explorer 重建、三窗任务隔离和空窗驻留仍未执行，完整 V04 不通过。

本版按 [ADR-0014](../adr/0014-same-document-session-renewal.md)补足续期、稳定资源归属、有限重试和关闭清理契约。[真实时钟续期独立消费者](../../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)的 x86/x64 编译与无窗口自检通过；随后[本机 x86 GUI 首次短测](../../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)在页面就绪前因会话关闭退出 1，任务未启动，x64 GUI 与真实超过 30 分钟/八小时运行未执行。[v2 修正夹具](../../verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/README.md)已完成两架构编译、每架构 45 项无窗口阶段自检、106 项分析器合成检查及 8 项运行输入绑定检查；随后[两架构首次 GUI](../../verification/production-validation/preview11-real-renewal-v2-smoke-2026-09-26/README.md)均因初始 60 秒未就绪退出 1，bootstrap 和任务调用为 0，真实续期未通过；模拟八小时轮换仍不能证明真实长稳。前序版本的桌面、Win7、完整源码重编和受干扰八小时记录不迁移到 preview.11。Win7 前两轮传输失败保留；[第三轮 v3 客体](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt3-2026-09-26/README.md)通过传输与运行前身份检查，Starter 就绪退出 0，Workbench 默认入口就绪后关闭超时，React 及运行后载荷检查未执行；同文档超过 30 分钟与八小时运行、完整 V02、Win7 SP1 x86、物理设备、当前完整源码交付及资源/性能门槛继续待验收。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

preview.11 的后续 V06 增补：[四阶段独立升级构建](../../verification/production-validation/preview11-upgrade-interruption-2026-09-26/README.md)在 x86/x64 共八次退出 0，应用/数据文件保留，GUI 尚未执行。真实终止安装进程后，正式校验及重新安装均失败；旧内容和业务数据虽可从原位与备份核对恢复，当前工具没有自动恢复入口。安装事务的持久化与恢复门槛未通过，工程交付结论保持未通过。

## 2026-09-26 preview.10 预检

2026-09-26 当次开发版为 preview.10 公共菜单、快捷键和显式驻留切片；[预检](../../verification/production-validation/preview10-residency-preflight-2026-09-26/README.md)仅通过双架构 Release 编译、原生契约和独立安装消费。本版真实桌面、Win7、无 Git 完整源码复现和完整 V04 均待执行，未形成新的生产准入批准。下方 preview.8 判定保持原输入范围。

## 2026-09-25 preview.8 增补判定

2026-09-26 开发进入 preview.9 公共托盘切片；本节 preview.8 工程和桌面证据保持原身份，不转移为新版本通过。[preview.8 Win7 x86 Demo GUI](../../verification/production-validation/win7-sp1-x86-qemu-preview8-demo-ui-2026-09-25/README.md)另已补足列表滚动、公共任务完成/取消的人工截图与串口核验；完整 V02 和 V04 仍未完成。

**候选源码工程复现通过；指定范围生产未通过；相对 Electron 的整体采用价值未判定。** `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。本节只评估 preview.8，旧版结果仍按各自来源保留。

- [冻结候选完整源码包](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)从两个全新无 Git 解压目录完成 x86/x64 全量底层依赖重编。SOUI 编译改在 `out/` 副本进行，重编后两目录的完整源码清单仍分别通过 30,794 文件；四组 x86/x64 × Release/Debug 应用构建、完整测试与三入口真实桌面短测均通过，重编 DLL 的两架构启动也通过。候选 ZIP 加入证据文档后的最终源码包的上一轮快照已全新解压并通过 30,953 文件清单；加入独立项目与公共任务证据后的当前清单覆盖 31,056 文件；与受测候选相比，非文档/验证代码文件散列变化为 0。
- [仓库外新项目](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)在 x86/x64 Release 构建并真实启动；[独立业务项目升级、回退、再升级](../../verification/production-validation/preview8-independent-upgrade-2026-09-25/README.md)的四阶段在两架构都通过，业务文件散列保持不变。[重编 x86 DLL 的 Win7 SP1 x86 QEMU 三入口](../../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)经 24 份串口结果及反例自检通过。[最终源码包生成的独立项目 Win7 SP1 x86 QEMU 短测](../../verification/production-validation/win7-sp1-x86-qemu-preview8-independent-consumer-2026-09-25/README.md)核验 71 文件、15 份串口结果和正常关闭；首次测试脚本误判保留为失败，修正后重新运行通过。[同一源码包构建的公共 SDK 独立页面消费者](../../verification/production-validation/win7-sp1-x86-qemu-preview8-public-task-2026-09-25/README.md)又在 Win7 SP1 x86 QEMU 完成 Bridge 三轮、公开任务进度/取消与容量恢复，76 文件载荷、17 份串口结果和事件序列均通过独立核验。
- 工程短测不关闭物理 Windows 7 SP1 x86、完整 Demo 业务链、Bridge 超时根因、GPU 与长期资源、同机实际呈现性能、内核安全维护、授权、签名安装与正式发布责任。先完成这些门槛，再决定是否向首批生产项目推荐。

## 2026-09-25 preview.7 增补判定

**工程交付部分通过；指定范围生产未通过；相对 Electron 的整体采用价值未判定。** `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。本节只新增 preview.7 的结果；下方 preview.5/.6 结果仍以各自来源为准。

- [搬移源码的底层重编诊断](../../verification/production-validation/preview6-relocated-dependency-encoding-2026-09-25/README.md)发现 preview.6 无 Git 源包在本机 CP936 下重编 x86 Miniblink 会因 UTF-8 文件误读失败；全局 `/utf-8` 则令旧源码产生大量编码警告。preview.7 给五个文件定点添加 BOM，新增构建告警门槛。修改后的搬移目录中，x86/x64 SOUI、Skia、Miniblink 重编及两架构 Starter/Workbench 启动均通过；该目录已被诊断性修改，不等于最终 preview.7 整包的原封重建。
- [preview.7 双架构 Release 回归](../../verification/production-validation/preview6-relocated-dependency-encoding-2026-09-25/README.md)完成完整应用编译、Web/原生契约与三入口 smoke；固定运行包与上述重编内核分开记录。[重编 x86 内核的 Win7 SP1 x86 QEMU 三入口短测](../../verification/production-validation/win7-sp1-x86-qemu-preview7-rebuilt-source-2026-09-25/README.md)再次通过：Starter、Workbench 默认页和 React 页退出 0，24 份串口结果经长度、散列、系统身份与日志内容复核。这是诊断组合，未覆盖新版公共消费者、八小时或物理 Win7 设备。
- [无 Git 完整源码快照](../../verification/production-validation/preview7-full-source-stage-2026-09-25/README.md)在清单验证后重新完成 x86/x64 Release 应用构建、完整测试与三项真实桌面 smoke；preview.7 SDK 包 408 文件可与工作树源逐项比对，安装/升级/回退包测试 14/14。这关闭应用源码搬移短验收。后续[最终源码包双架构底层重编](../../verification/production-validation/preview7-final-source-bottom-rebuild-2026-09-25/README.md)从 SHA-256 `A4F0703A4C6F6E13DC4A07BC59174D347487622143AAB51E82B3D3F4190D802E` 的原封 ZIP 分别全新解压，完成 x86/x64 的全部底层依赖、应用编译、完整测试和重编 DLL 启动；[重编 x86 载荷在 Win7 SP1 x86 QEMU](../../verification/production-validation/win7-sp1-x86-qemu-preview7-final-source-2026-09-25/README.md)三入口短测也通过。旧 SOUI 构建会在源码树增加编译产物，导致编译后清单文件集合复验失败；逐文件比对确认原清单源码未改变。Debug、物理目标机和完整交付流程仍缺证据。
- [S02 交替配对记录](../../verification/production-validation/benchmark-s02-series-interference-2026-09-25/README.md)仍受外部 QEMU 干扰，未取得五对隔离样本，也缺实际呈现；最小包体积历史样本只能支持体积差异，不能推断流畅性或总体采用价值。

下一步修正旧 SOUI 构建在源码树生成产物的副作用，补齐 Debug/四组配置与目标机安装升级链；随后继续关闭资源增长、Bridge/GPU 生命周期、同机呈现性能、物理目标机、安全维护和授权门槛。未完成这些证据前不得发布为生产就绪。

## 2026-09-25 preview.6 增补判定

本增补沿用下方三项问题，保留 preview.5 原判定与失败样本。**独立交付部分通过、指定范围生产未通过、相对 Electron 的整体采用价值未判定**；`sourceDeliveryVerified=false`、`releaseReady=false`。

- preview.6 [双架构安装包独立消费者](../../verification/production-validation/preview6-independent-consumer-2026-09-25/README.md)、[无 Git 的完整源码搬移与双架构重编](../../verification/production-validation/preview6-source-relocation-2026-09-25/README.md)已通过。搬移后底层依赖未重编且 GUI 未在该目录启动，不能将源码交付门槛写成完全通过。
- 实际 Win7 SP1 x86 QEMU 上，[preview.6 三应用入口](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-v4-2026-09-25/README.md)、[冻结公共任务消费者](../../verification/production-validation/win7-sp1-x86-qemu-public-task-2026-09-25/README.md)、[公共剪贴板/窗口中文往返](../../verification/production-validation/win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)、[窗口线程停滞后的超时恢复](../../verification/production-validation/win7-sp1-x86-qemu-title-timeout-v2-2026-09-25/README.md)及[目录对话框实际打开并取消](../../verification/production-validation/win7-sp1-x86-qemu-directory-v2-2026-09-25/README.md)各自的短测通过。它们来源不同，不能跨候选合成一个完整通过版本。
- preview.5 [八小时混合运行](../../verification/production-validation/preview5-eight-hour-soak-2026-09-25/README.md)完成 1,690 轮、23,664 步、退出 0，但私有内存每小时中位数由约 64.1 MiB 升至 136.7 MiB，且与外部负载重叠；资源/时延门槛未评估，亦不能覆盖 preview.6。真实页面交互、完整业务、物理 Win7 x86、隔离的最终候选长测和 SOUI/Electron 同机配对性能仍待执行。
- [同机 SOUI/Electron S02 配对链路预检](../../verification/production-validation/benchmark-s02-full-preflight-2026-09-25/README.md)已有三对真实 61 秒滚动样本，页面、输入、完整资源副本、进程树和身份预检通过；第 1 对与 QEMU 并行，只保留诊断。另两对无 QEMU 的样本给出初步内存/CPU 分布，但缺实际屏幕呈现、五对重复、冷/热启动和目标系统验证，`benchmarkValid=false`，不能判定相对 Electron 的整体价值。

| 必须回答的问题 | 当前判定 | 决定性的现有证据与缺口 |
|---|---|---|
| SDK 能否独立交付 | **部分通过，尚未形成完整交付通过** | preview.5 [双架构完整构建、三项 smoke、仓库外生成项目及无 Git 完整源码搬移后重新构建](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)已通过。preview.5 的[Win7 x86 QEMU 四模式公共消费者短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)已通过；preview.4 的[x64 Release 业务小样](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)升级回退仍保留原候选身份；新版 x86 Bridge 长循环两次超时，物理目标机尚未执行。底层依赖的搬移后源码重编译、Debug 和目标机交付仍未闭环。 |
| 指定范围能否生产使用 | **未通过** | preview.4 已将修正 HTTPS 校验的内核纳入固定包，[双架构 12 项正负向测试](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)通过；[新固定包生命周期记录](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)完成双架构各 100 次重载及四种模式各 1,000 次，但所有资源门仍未评估，三窗口与 Bridge 分段内存总体上升。GPU 关闭曾出现访问冲突，历史双架构 GPU 重建 30 轮仍有 USER 对象逐轮增长且 Runtime 关闭后残留。[GPU 专项](gpu-lifecycle.md)未达到资源门槛。preview.5 的[任务表容量硬边界](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)已有双架构测试，但公共任务业务链还未完整验收。[Win7 SP1 x86 QEMU 四模式短测](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)与[模板/Demo 三项短测](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)仅对应 preview.4；preview.5 在 Win7 SP1 x86 QEMU 的[三应用入口](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)、[公共消费者四模式三轮](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)、[另一诊断来源的 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)已通过各自操作门。任务满载业务、8 小时混合负载、物理 Win7 x86 设备与 DPI/IME、完整业务任务、实际信任根/代理、授权和旧 TLS 组件维护仍缺证据。 |
| 相对 Electron 的采用价值是否成立 | **最小宿主体积有样本，整体仍不能判定** | [同页面 x64 最小运行包测量](../../verification/production-validation/benchmark-size-preview5-2026-09-24/README.md)得出 SOUI/Electron 解压后 10.04%、相同 ZIP 算法压缩后 9.42%；两包各自从交付目录完成 S01 写入探针。它不包含完整业务产品、安装签名与授权闭环。尚未取得正式两端配对输入到呈现、帧间隔、进程树资源和维护工时原始样本，不能从体积单项推导采用价值。 |

preview.5 的[固定载荷 x86 生命周期复测](../../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)新增明确阻塞：Bridge 目标 1,000 轮两次分别在第 86 和 977 轮挂载阶段超时，监督器终止进程；同载荷后续三次诊断完成 1,000 轮，其中一次保留原 20 秒门限，一次并行自有 QEMU 快照。成功样本不能覆盖失败，且均未捕获停滞栈。宿主 100 次重载的 Private Bytes 末点增量 7.0 MiB，低于 16 MiB 数值线，但句柄与线程增加且资源整体门未评估。两次超时与其他 QEMU 负载重叠；可控 QEMU 负载测试通过，说明并行 QEMU 本身不是充分复现条件。根因仍需抓栈定位，Win7 虚拟机内仍缺 Bridge 千轮和完整业务长测。

[带阶段日志的另一诊断载荷](../../verification/production-validation/preview5-mount-phase-2026-09-24/README.md)在 x86 宿主完成 3 次预热加 1,000 次 Bridge、无超时，并保留 10,030 条逐阶段事件；第 977 轮这次通过，8 秒停滞观察器未触发。其源码/消费者载荷与上述冻结失败样本不同，不能用这次成功消除生产阻塞。该诊断运行的每百轮 Private Bytes 中位数约 79.4→96.0 MiB，资源归属仍未解释。

[带超时现场保存的新诊断载荷](../../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)在 x86 宿主又完成两次各 1,000 轮 Bridge，均保留 10,030 条阶段事件，20 秒单步门限未放宽；因未复现超时，没有得到 SDK 故障栈。受控 x86 进程已证明抓栈工具可用，但这不能诊断原两次失败。新载荷与原冻结样本源码指纹及构建载荷不同，分别统计；资源门和生产判定不变。

同一[诊断记录](../../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)还包含一次原失败源码重新编译后的 x86 Bridge 千轮成功对照。其源码指纹相同，但消费者构建与运行载荷 SHA-256 均不同；只读 8 秒停滞观察器未触发。不能将此成功重新标记为原冻结载荷通过，更不能撤销两次失败。三组诊断和旧失败均只在宿主 Windows 11 上运行。

[Win7 SP1 x86 QEMU 新来源 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)完成 3 次预热、100 次测量，退出 0、109 个步骤完整，Private Bytes 预热末点至最后一轮增加 14,876,672 字节，低于 16 MiB 数值线。句柄增加 57、线程增加 4；资源整体门仍未评估。QEMU 结果不能推广到物理设备，也不覆盖 Bridge 千轮故障。

[preview.5 原生任务容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)已直接在 Win7 SP1 x86 QEMU 来宾退出 0，串口证据核对系统身份与测试 EXE 散列。这只关闭原生容量边界单测；公共任务服务的满载业务链、长时并发和物理设备仍待验收。

下一阶段按风险顺序收敛：先定位 x86 Bridge 长循环超时和 GPU USER 残留，完成当前公共连接器的双架构生命周期/资源门槛；并行补齐公共任务、桌面模块与六组 Demo 场景；随后冻结共享负载与 Electron 宿主，完成同机性能/体积对照；最后跑 8 小时、目标设备与安装升级回退、安全授权和发布责任审核。每阶段保留失败样本，改变源码或内核后重跑受影响项。正式报告仍分别给出工程交付、指定范围生产和采用价值三个结论。
