# 候选支持范围与设备矩阵

## 2026-09-27 preview.15 当前候选判定

状态：**预览，尚无生产支持组合**。当前 SDK Core 419 文件身份保持冻结；Demo 的 V02 持续任务使用 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`。下表只陈述已绑定该输入或明确标出的旧输入结果；不同源码的短测不合并为一个通过项。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

| 组合 | 当前原始证据 | 可成立的范围与未完成项 |
| --- | --- | --- |
| Windows 11 开发机 x64 Release | [正式应用构建](../../verification/production-validation/v02-continuous-task-official-build-2026-09-27/README.md) buildId `5ddcec93-d244-456c-877b-1bdeabee568d`；[三入口短测](../../verification/production-validation/v02-current-desktop-smoke-2026-09-27/README.md)；[V02 可见交互单轮](../../verification/production-validation/v02-current-visible-x64-2026-09-27/README.md) | 当前 Demo 三入口各原生退出 0；同一自然前台 V02 页面单轮完成万条列表滚轮、30 秒公共任务运行中拼音提交“中文”、缩放、内层列表滚动和自然关闭。尚无完整 V02、多轮性能、DPI 多屏或长稳支持结论。 |
| Windows 11 开发机 x86 Release | 同一[正式构建和三入口短测](../../verification/production-validation/v02-current-desktop-smoke-2026-09-27/README.md)，buildId `f0c38056-058a-456e-be99-17446127f7ec`；[V02 可见交互单轮](../../verification/production-validation/v02-current-visible-x86-2026-09-27/README.md) | 当前 Demo 三入口及一轮真实 V02 操作原生退出 0，任务运行期间有中文提交、缩放和内层滚动截图。这是在 64 位 Windows 上运行的 x86 进程；不证明独立 32 位系统，长期资源、DPI 多屏与生产范围仍未验收。 |
| Windows 7 SP1 x86 QEMU | 当前 718 文件源码、buildId `f0c38056-058a-456e-be99-17446127f7ec` 的[应用短测](../../verification/production-validation/win7-sp1-x86-v02-current-app-smoke-2026-09-28/README.md)、[可见页面](../../verification/production-validation/win7-sp1-x86-v02-visible-page-2026-09-28/README.md)及[滚轮前后截图](../../verification/production-validation/win7-sp1-x86-v02-layout-wheel-before-after-2026-09-28/README.md)；新 IME/缩放/任务尝试的[隔离中断](../../verification/production-validation/win7-sp1-x86-v02-interaction-gate-interrupted-2026-09-28/README.md) | 当前构建的一次短启动/完整关闭、临时 1024×768 桌面中数据页可见和原生滚轮造成的页面位置变化已证明。中文 IME 组合、缩放、任务并行未执行；新尝试在发命令前因外部 QEMU 停止，不能计兼容失败或通过。原生 800×600、物理机、DPI、长期资源和独立业务仍待验。 |
| Windows 7 SP1 x86 物理机及其他目标系统 | 无当前候选的目标设备全矩阵记录 | 未验证；QEMU 短测不能代替物理输入、显示、安装、网络和系统策略。 |

当前 V02 [输入探针准备](../../verification/production-validation/v02-current-gui-preparation-2026-09-27/README.md)、[双架构编译](../../verification/production-validation/v02-current-input-probe-build-2026-09-27/README.md)和[各五项错误输入拒绝](../../verification/production-validation/v02-current-input-probe-rejections-2026-09-27/README.md)建立了真实输入的验收手段；本机两架构的可见结果另见表中原件，不能用单轮操作推出性能或平台支持。当前同机 Electron 的 ≥5 对交替性能、完整运行包比较、独立安装消费者真实续期、每模式 1,000 轮、100 次重载、隔离 8 小时混合负载、内核安全与授权责任均未形成准入证据。以下旧版本矩阵保留用于回溯，不作为 preview.15 的放行依据。

历史 preview.13 候选已取得[安装事务恢复](../../verification/production-validation/preview13-install-recovery-2026-09-26/README.md)、[已知旧现场副本恢复及独立账本四配置编译](../../verification/production-validation/preview13-legacy-recovery-build-2026-09-26/README.md)，以及[完整源码解压后 SDK/Starter/Workbench x86/x64 Debug/Release 构建、44 项原生与 8 项交付身份测试](../../verification/production-validation/preview13-full-source-build-2026-09-26/README.md)证据。该版应用 GUI、Win7 和完整交互矩阵未重验，不扩大设备支持状态。下述 preview.11 及以前的结果均为各自冻结输入。

2026-09-26 preview.11 增补：[当前预检](../../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)完成本机 x86/x64 Release SDK、Starter、Workbench 编译、独立 SDK 安装与仓库外 `session-consumer` 编译/运行，每架构 10 项 CTest 通过，其中原生续期包含 14 组契约。最终汇总核验 Web 62 项、包行为 14 项、交付身份 7 项通过、五份锁文件全依赖审计漏洞总数均为 0；418 文件 SDK 首次打包、包内散列及源码匹配通过。生成的独立 `RenewalStarter` 两架构 Release 编译与项目校验均退出 0。

[本版真实桌面短测](../../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)随后在 Windows 11 宿主串行完成：x86/x64 窗口线程停滞与目录程序化取消共四项、Starter/Workbench 默认与 React 共六入口、独立 `RenewalStarter` 两项短启动均取得退出码 0。x86 公共托盘消费者因 `E_MENU_FOREGROUND` 退出 1，菜单尚未显示；x86 Workbench 驻留因 slot 2 未在 45 秒内就绪退出 3，尚未进入 smoke 阶段，slot 3 状态未判定。两次失败保留，x64 菜单/驻留尚未执行。

首次 x86 窗口线程超时探针缺退出码的旧样本仍不计通过。Win7 前两轮传输失败保留；[第三轮 v3 客体](../../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt3-2026-09-26/README.md)通过传输与运行前身份检查，Starter 就绪退出 0，Workbench 默认入口就绪后关闭超时，React 及运行后载荷检查未执行；真实超过 30 分钟/八小时续期、完整 V02/V04、物理设备及资源/性能门槛仍未通过。[真实时钟续期独立消费者](../../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)的 x86/x64 编译与无窗口自检通过；随后[本机 x86 GUI 首次短测](../../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)在页面就绪前因会话关闭退出 1，任务未启动，x64 GUI 与真实超过 30 分钟/八小时运行未执行。[v2 修正夹具](../../verification/production-validation/preview11-real-renewal-v2-build-2026-09-26/README.md)已完成两架构编译、每架构 45 项无窗口阶段自检、106 项分析器合成检查及 8 项运行输入绑定检查；随后[两架构首次 GUI](../../verification/production-validation/preview11-real-renewal-v2-smoke-2026-09-26/README.md)均因初始 60 秒未就绪退出 1，bootstrap 和任务调用为 0，真实续期未通过；可控时钟轮换也不改变设备支持状态。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

2026-09-26 preview.10 历史增补：[本机 x86/x64 Release 编译、安装消费与非桌面交互预检](../../verification/production-validation/preview10-residency-preflight-2026-09-26/README.md)通过。新增公共菜单/快捷键/驻留当时尚无本版真实桌面或 Win7 结果，下述历史载荷不自动授予新版本兼容通过。

日期：2026-09-24。状态：采集中，不是正式支持声明。首批真实项目要求最低 Windows 7 SP1，包含 x86；不能仅凭本机 Windows 11 上的 x86 进程结果放行。

2026-09-25 增补：[preview.7 最终完整源码重编 x86 载荷](../../verification/production-validation/win7-sp1-x86-qemu-preview7-final-source-2026-09-25/README.md)已在 Windows 7 SP1 x86 QEMU 通过 Starter、Workbench 默认页和 React 页短启动，且来宾系统身份、载荷散列、退出码和 24 份串口结果复核通过。它不扩展为物理设备、完整业务或性能结论；下表各旧版本证据仍保留原身份。

preview.8 [无 Git 候选源码包双架构重编与四组应用构建](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)通过；[同源重编 x86 载荷](../../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)也在 QEMU 的 Windows 7 SP1 x86 完成三入口短测、24 份串口结果与篡改反例。另有[最终源码包生成的独立项目 Win7 SP1 x86 QEMU 短测](../../verification/production-validation/win7-sp1-x86-qemu-preview8-independent-consumer-2026-09-25/README.md)完成模板工程实际运行；首次脚本校验失败保留，修正后复测通过。[同一源码包构建的独立公共 SDK 页面消费者](../../verification/production-validation/win7-sp1-x86-qemu-preview8-public-task-2026-09-25/README.md)在 Win7 x86 QEMU 完成公开任务进度、取消、容量与恢复三轮短测。以下物理设备与体验要求不因虚拟机短测而降低。

## 已使用的设备

当前实际设备为 Windows 11 Pro 25H2，`10.0.26200.9457`，AMD Ryzen 9 9950X（16 核/32 逻辑处理器）。系统同时枚举 AMD Radeon、Intel Arc A380 及虚拟显示适配器；实际呈现所用设备、显示输出和 DPI 仍须逐个测试确认。不能从枚举到显卡或装有中文输入法直接推断功能已经通过。

[设备观察记录](../../verification/production-validation/environment-windows11-26200.json)包含系统、补丁号、CPU、显卡及驱动。性能采样还须登记电源计划、实际显示刷新率、当前 DPI、远程会话状态和活动 GPU；当前窗口生命周期结果不提供这些指标的通过结论。

## 历史组合与待验收项（非 preview.15 支持声明）

| 组合或能力 | 当前证据 | 状态与缺口 |
|---|---|---|
| preview.11 本机 x86/x64 Release | [当前预检](../../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)的编译、安装消费与契约通过；[真实桌面记录](../../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)补足四项原生桌面探针、六个应用入口及两个生成项目短启动，均退出 0 | 仅所列短测通过；x86 菜单前台激活和驻留 slot 2 就绪失败，x64 菜单/驻留未执行。当前 Debug、Win7、完整 V02/V04、超过 30 分钟同文档续期、八小时资源与完整交付仍待验收，不扩大支持声明 |
| 本机 x64 Release | preview.4 固定包公共消费者软件路径重建/重载/三窗口/Bridge 各 1,000 轮、另有静置 1 秒的重载 100 轮；模板及 Demo 默认/React 回归；历史 GPU 重建 30 轮暴露 USER 残留 | [新固定包生命周期](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)、[GPU 专项](gpu-lifecycle.md)和[当前应用回归](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)分别绑定身份；资源总体与业务长稳未通过 |
| 本机 x86 Release | preview.4 固定包公共消费者软件路径重载/销毁重建/三窗口/Bridge 各 1,000 轮、另有静置 1 秒的重载 100 轮；模板及 Demo 默认/React 回归；历史 GPU 重建 30 轮暴露 USER 残留 | [新固定包生命周期](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)与[GPU 专项](gpu-lifecycle.md)分别记录；资源门和业务长稳未通过，也不是独立 32 位系统的兼容结论 |
| 本机 x86/x64 Debug | preview.8 完整源码候选在本机两架构的 Debug 应用编译、完整测试和三入口桌面短测均通过，见[四组构建证据](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md) | 仅此候选的本机 Debug 短验收通过；目标系统 Debug、独立项目 Debug、资源与长期稳定仍未验证 |
| Windows 10 指定版本/补丁 | 无本轮目标机报告 | 待登记设备和执行；不得推广本机结果 |
| Windows 7 SP1 x86 | [preview.5 QEMU 应用短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)：6.1.7601/32-bit、模板与 Workbench 默认/React 三项退出码 0；[同版公共消费者短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)：Bridge、重载、重建、三窗口各 3 轮；[原生任务容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)退出码 0；[另一诊断源码 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)退出码 0，内存末点增量低于 16 MiB 数值线 | 虚拟机软件路径三应用、四模式短操作、原生容量单测及新来源 100 次重载操作通过；句柄/线程末点上升、资源整体门、Bridge 千轮、公共任务满载业务、物理设备、完整 Demo、安装签名、TLS 信任根/代理、中文 IME、DPI 与升级回退仍未通过或未验证，不能生产放行 |
| Windows 7 SP1 x64 | 只锁定了最低系统与 x86 必需组合；x64 是否为首批必需尚待产品矩阵确定 | 若列入支持，须另行完成目标机验收；不能沿用 x86 或 Windows 11 结果 |
| 其他 Windows 11 版本/补丁 | 无本轮目标机报告 | 待登记设备和执行 |
| 100/125/150/200% DPI | 未执行控件、坐标和输入定位验收 | 未验证 |
| 混合 DPI 多屏 | 系统枚举有两种活动显卡输出；未做窗口移动/缩放/断连 | 未验证 |
| 中文 IME | 配置观察不等于输入正确 | 组合输入、候选框定位、光标、撤销及重载均待验证 |
| 键盘和辅助功能 | 尚无本轮数据 | 焦点顺序、快捷键冲突、屏幕阅读器语义待验证 |
| 安装、签名及系统策略 | 尚无目标产品候选 | 需真实签名产物、受限账户、升级/回退后运行记录 |

新增设备报告应包含设备标签（避免个人主机名）、精确系统及驱动、源码与载荷身份、架构/配置、场景及原始记录。不支持或未执行的组合保留原状态；表格填满不能替代测试通过。

完整目标及尚未满足的设备需求保留在[执行记录](progress.md)，最终只按实际通过的组合生成支持声明。
