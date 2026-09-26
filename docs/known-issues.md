# 已知限制与生产门槛

本交付解决分层、接入、自行构建和验证的问题，仍为预览版本。

preview.11 Win7 SP1 x86 两次客体在传输集合检查阶段失败，尚未启动三个应用；[诊断确认卷根系统目录造成额外项](../verification/production-validation/win7-sp1-x86-qemu-preview11-apps-attempt2-2026-09-26/README.md)。输入子目录布局已冻结，仍待空闲后复测；原有 Win7 通过仅对应各自历史版本。

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
| DPI、多屏、输入法、辅助功能 | 需要实际产品和设备回归 |
| 浏览器能力 | Miniblink49 分支；页面按 Chrome 49 构建，不能假定现代 Web API 可用 |
| 不可信网页 | 模板面向本地受控页面；远程不可信内容需专项评估内核与原生能力边界 |
| 内核网络及安全维护 | preview.4 固定 DLL 已换入双架构重编译候选并再次完成回环 HTTPS 正负向测试；系统信任根、代理、动态子资源、逐组件补丁覆盖及维护责任仍未闭环，见[核对记录](acceptance/kernel-maintenance.md) |
| API / ABI | preview 接口可能迁移，不保证跨编译器 C++ ABI |
| 公共驻留能力 | preview.10 在托盘基础上增加公共菜单、全局快捷键及显式 Workbench 三窗口驻留。preview.11 [真实 x86 短测](../verification/production-validation/preview11-desktop-execution-2026-09-26/README.md)中，托盘消费者在菜单显示前返回 `E_MENU_FOREGROUND`、退出 1；Workbench 驻留 slot 2 达到内部 45 秒就绪期限、退出 3，尚未进入 smoke 阶段，slot 3 未判定。两项清理完成，根因尚未确证；x64 菜单/驻留、实际菜单/快捷键输入、Explorer 重启、三窗任务隔离、模态关闭、空窗驻留及 Win7 x86 仍未执行，V04 不通过。此前 preview.9 短测不覆盖这些新增行为 |
| 同文档长会话续接 | preview.11 已实现显式公共续期、稳定授权资源归属与 Web 自动续期，[原生契约及安装后独立消费者](../verification/production-validation/preview11-session-renewal-preflight-2026-09-26/README.md)在两架构通过，设计见 [ADR-0014](adr/0014-same-document-session-renewal.md)。[真实时钟续期独立消费者](../verification/production-validation/preview11-real-renewal-build-2026-09-26/README.md)的 x86/x64 编译与无窗口自检通过；随后[本机 x86 GUI 首次短测](../verification/production-validation/preview11-real-renewal-smoke-attempt1-2026-09-26/README.md)在页面就绪前因会话关闭退出 1，任务未启动，x64 GUI 与真实超过 30 分钟/八小时运行未执行；模拟时钟和短测不能替代任务/订阅跨轮换保留的真实长时验收。长模态或休眠跨过期限仍明确失败，完整门槛未关闭 |
| 依赖授权 | 已保留来源、散列及授权文本；精确二进制再分发审核尚未完成 |
| Web 构建依赖审计 | Vite 6.4.3 / PostCSS 8.5.23 兼容性升级后，本次 npm audit：SDK、模板、Demo 均为 0 项；完整结果在 verification/*-audit.json。后续更新继续由锁文件、Dependabot 和发布验收共同约束 |
| 自动验证 | 按当前维护决策不配置远程 CI；每个 PR 必须提供适用的本机构建、测试和真实桌面验收证据 |
| 分支保护 | 当前私有仓库方案不支持 GitHub 分支保护；维护者通过 PR、CODEOWNERS、Squash 合并和人工评审执行流程约束 |
| 安装、更新、签名 | 由实际项目配置，并验证升级和回退 |

发布负责人应针对具体产品逐项补证据、处理未关闭缺陷，再确定生产版本。验收结果保持 `sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
