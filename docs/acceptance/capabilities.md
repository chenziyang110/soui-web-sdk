# 公共能力与验收映射

日期：2026-09-24。开发版本：`0.3.0-preview.5`。这是[完整方案](../roadmap/0.3-sdk-validation.md)的逐项执行入口，不是生产支持声明。preview.5 双架构构建与短测源码指纹为 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`，见[任务容量证据](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)；本版 Win7 x86 的[三应用](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)与[四模式公共消费者](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)短测独立归档。带超时诊断的新源码又在[Win7 SP1 x86 QEMU 完成 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)，来源不同，不与原失败载荷混算。preview.4 的千次生命周期证据保留原候选身份，不跨版本视为通过。

状态为“待实现、部分实现、已实现待验收、通过当前范围、失败、待外部验证”。`通过当前范围` 只对应本行所列场景；同组尚有缺口时，整组不通过。不把 Demo 的注册或 TypeScript 声明当作公共模块实现。

## V01 通信与长任务

| ID | 公共入口/所有权 | 当前状态与证据 | 下一项必需验证 |
|---|---|---|---|
| V01-01 连接与关联 | `json_bridge`、`connectSouiNative`、`SouiNativeClientImpl.call` | 已修复 x86 `bool` 返回 ABI；preview.4 固定包两架构 Bridge 1,000 轮及四模式短回归通过。preview.5 [Win7 SP1 x86 QEMU Bridge 3 轮](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)与模板/Demo 三入口短测通过 | 真实业务方法、资源关闭门槛、物理 Win7 x86 设备与完整业务链 |
| V01-02 异步响应 | Host Bridge worker | 双架构公共消费者短测通过 32 并发关联、200 ms 原生等待期间页面定时器推进；这不等于已测输入延迟或公共长任务 | 真实输入/呈现延迟、公共任务服务、长时间会话 |
| V01-03 事件与释放 | `SouiNativePort.onEvent`；Host event sink | 部分实现：模板已接入真实事件连接器；统一 dispose、可观察订阅、迟到建立补偿和幂等移除已有单元验证 | 原生任务服务的真实事件、重载/关闭竞态、丢失确认后的会话清理 |
| V01-04 任务与取消 | `task_store` 为底层状态；待审查分支新增公共 `task_service`，Demo 的既有业务适配仍独立 | 已实现待验收：公共注册、2 个工作线程、32 个未完成任务、进度和协作取消通过 x86/x64 原生契约；Demo 新增公开 `task.start/query/subscribe/cancel` 计数场景，双架构无窗口适配器测试通过；独立安装消费者仅完成双架构编译与类型检查 | 独立页面和 Demo 页面真实运行、取消/完成竞态、任务中关窗与 Win7 x86 |
| V01-05 容量与错误 | Bridge 参数边界；Host 请求预算和 resolver；客户端等待上限 | preview.5 的 `task_store` 硬上限、重复/冲突、取消占位、并发准入和扫描 `E_BUSY` 的 x86/x64 测试通过；[Win7 SP1 x86 QEMU 原生容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)同样退出 0。模板/Workbench 三入口也在该系统短测通过；此前公共消费者覆盖 256 普通请求上限、控制容量和错误恢复 | 直接公共服务消费、实际排队超时与关闭竞态、Win7 上新版任务满载业务链 |
| V01-06 会话长寿命 | `permission_store` / `json_bridge` | 已实现待验收：凭据存在期限，长会话恢复契约未闭环 | 超过 30 分钟的过期、续接与原任务归属；8 小时不能靠重载规避 |

连接器与业务方法类型拆分按 [ADR-0004](../adr/0004-web-connection-lifecycle.md) 实施。默认 `SouiMethodMap` 仍包含 Demo 服务字段，最小模板通过自己的 `AppMethods` 接入；后续迁移须保留明确兼容说明。

2026-09-25 增量：V01-02/03/05 的公共任务处理器、进度事件及 32 项准入已在[待审查 PR #26](https://github.com/chenziyang110/soui-web-sdk/pull/26)完成双架构原生契约；独立消费者的 Web/原生载荷准备通过，且 Web 类型检查现绑定已安装 SDK 的声明文件。以上仍不构成真实桌面、Demo 或目标系统通过证据，表中 preview.5 冻结包的结果继续按原身份引用。

同一开发分支的 Demo 已加可见公开任务场景，公开任务和旧业务任务使用不同 ID 前缀；x86/x64 的 `demo.native.public-task` 无窗口测试覆盖启动、订阅事件、成功结果、取消及会话撤销。Web 构建和严格 TypeScript 检查通过。该测试没有创建 WebView，不能替代 Demo 页面交互或 Win7 客体验收。

## V02–V04 交互与桌面

| ID | 公共入口/所有权 | 当前状态与证据 | 下一项必需验证 |
|---|---|---|---|
| V02-01 万条列表与动画 | 共享页面 + Host 呈现 | 确定性 10,000 行共享页面和双端宿主已构建、完成结果落盘探针；尚无真实输入和帧样本 | 冻结相同内容区、输入轨迹和实际加载脚本；同机采样与物理呈现核验 |
| V02-02 中文输入与 resize | Host 输入/坐标/呈现 | 已实现待验收；无本轮完整实机结果 | 组合输入、候选框、光标、连续缩放、任务期间输入和滚动 |
| V02-03 后台效率 | Host 显示状态与调度 | 已实现待验收；无候选性能结论 | 可见空闲、最小化、恢复、CPU 与帧间隔；GPU/CPU 路径分别登记 |
| V03-01 窗口操作 | SDK 有 `window_service`，通用 JSON 注册仍在 Demo | 部分实现 | 公共模块直接启用、状态事件、错误参数、关闭中重复请求 |
| V03-02 对话框 | SDK 有 `directory_picker`，注册/部分服务仍在 Demo | 部分实现；目录路径 Unicode 转换已修复并通过 x86/x64 原生边界测试 | 公共文件/目录选择的真实 UI、用户取消、受限账户、Owner 与线程归属 |
| V03-03 剪贴板 | SDK 有 `clipboard_service.write_text(owner, context, text)`，注册仍在 Demo | 部分实现；空所有者导致提交失败的问题已修正，错误编码和无效 HWND 的 x86/x64 拒绝测试通过 | Win7 客体真实写入/读取、公共 Web 注册、占用失败、Unicode、大文本、权限拒绝 |
| V03-04 屏幕与外部打开 | 公共 screen/shell 模块 | 待完成统一公共契约及消费者 | 混合 DPI、多屏移动/断连、URL/路径校验与操作结果 |
| V04-01 三窗口独立工作 | 进程 Runtime + 各窗口 Host/Bridge/session | 通过当前范围：preview.4 固定包 x64/x86 各 1,000 轮正常退出，关闭中间窗、另两窗重载和中间窗重建 | 资源所有权、任务和事件并存时关闭隔离；目标 32 位与旧版 Windows 实机 |
| V04-02 驻留能力 | 公共托盘、菜单、快捷键模块 | 待实现并独立验收 | 冲突/失败、Explorer 重启、无窗口驻留、最后退出回收 |

## V05 资源、故障与安全边界

| ID | 公共入口/所有权 | 当前状态与证据 | 下一项必需验证 |
|---|---|---|---|
| V05-01 同进程重建 | Host `destroy/start` + 进程 Runtime | preview.2 崩溃/挂起已复现；preview.4 固定包的软件路径 x64/x86 重建、三窗口和 Bridge 各 1,000 轮正常退出 | 资源所有权和 8 小时混合负载，不用进程重启代替 |
| V05-02 Runtime 最终退出 | `shutdown_miniblink_runtime` | 通过当前范围：有 Host 时拒绝、全部关闭后成功、重复关闭幂等、关闭后拒绝重启 | 残留任务、事件回调及并发关闭压力；单独进程启动退出模式 |
| V05-03 窗口级对象释放 | Host + Miniblink 补丁 0001–0005 | preview.4 双架构各 100 次重载末点内存增量低于 16 MiB 数值线；双架构千次重载/重建正常退出。preview.5 [Win7 SP1 x86 QEMU 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)也低于 16 MiB 数值线，但句柄和线程末点增加。历史 GPU 两架构 30 轮 USER 持续增长且 Runtime 关闭后残留 | 分段资源趋势、GPU USER 对象所有权、软件路径线程/USER 残留、其他模式与 8 小时混合负载；全部 `resourceGate` 仍未评估 |
| V05-04 故障与恢复 | 监督器已有退出码/超时/缺步骤检查 | 部分实现：[超时诊断](../../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)现记录已读日志偏移与未读内容，并可选抓取 x86 原生栈/转储；受控进程验证抓取链路，SDK 故障尚未复现；原始失败记录保留 | DLL/资源缺失、服务失败、迟到响应、真实挂载故障栈、重启后业务恢复 |
| V05-05 8 小时业务运行 | 固定混合负载及分时采样 | [独立消费者混合生命周期预检](../../verification/production-validation/preview5-mixed-soak-preflight-2026-09-24/README.md)已在 x86/x64 完成短测，覆盖 Bridge、重载、最小化/恢复、三窗口和空闲；真实公共任务及 8 小时结果未完成 | 操作/公共任务进度与取消/重载/最小化/空闲组合，连续 8 小时无无界积压或响应持续恶化 |
| V05-06 网络与内容边界 | Host 可信文档与底层网络 | preview.4 固定包的[回环 HTTPS 十二项](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)覆盖证书链、主机名及重定向；[内核核对](kernel-maintenance.md)保留生产阻塞项 | 实际信任根/代理、动态子资源、注入内容、越权、来源边界与持续维护 |

## V06 独立接入、维护与最终价值

| ID | 公共入口/所有权 | 当前状态与证据 | 下一项必需验证 |
|---|---|---|---|
| V06-01 仓库外新项目 | `New-Project.ps1`、模板、`sdk-lock.json` | preview.4 的[独立项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)完成包校验、仓库外生成、x86/x64 Release 编译和真实启动 | 加入真实业务方法的小样、Debug 与目标设备运行 |
| V06-02 升级与回退 | `tools/sdk/soui-sdk.cjs` | [仓库外业务小样](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)本机 x64 Release 完成 preview.3 → preview.4 → 回退 → 再升级，每段重编译、实际启动关闭及五个业务文件保留 | 中断/冲突安全失败、x86/Debug 与目标机升级链 |
| V06-03 依赖及源码交付 | 构建/打包/证据工具 | 两架构底层依赖在原源码树已重建；[当次完整源码包](../../verification/production-validation/preview4-source-2026-09-23/README.md)于仓库外通过校验和双架构 Release 应用编译/启动 | 刷新最终源码清单与 ZIP、搬移目录重建底层依赖、Debug/四组配置、精确载荷绑定及依赖 smoke v2 |
| V06-04 系统和安装 | [支持矩阵](support-matrix.md) | Win7 SP1 x86 QEMU 已有三应用、公共消费者四模式短测、原生任务容量及另一个来源的 100 次重载；不推广到物理机 | 指定系统物理设备、DPI、多屏、IME、签名安装、受限账户及回退 |
| V06-05 可持续维护 | [内核核对](kernel-maintenance.md)、许可与发布流程 | 部分来源已识别；维护/授权/负责人门槛未关闭 | 组件维护台账、依赖升级演练、许可审阅、实际发布责任确认 |
| VALUE-01 Electron 对照 | 两端共享负载和原生宿主 | 共享页面及 SOUI/Electron 最小宿主已实现；[双端结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)通过，但不是性能样本 | 正式同机交替运行；启动/输入/帧/CPU/内存/体积原始数据与报告，不牺牲对照安全设置 |
| VALUE-02 采用成本 | 独立业务小样与升级记录 | 待采集 | 实际接入工时、原生适配量、兼容补丁、升级/调试及长期维护成本 |

最终分别生成工程交付、指定范围生产验收、相对 Electron 采用价值三个结论。任何一行的短样本通过都不能直接设置 `releaseReady`。负责人以仓库 CODEOWNERS 和后续维护者确认记录为准，本表只标技术所有权，不虚构人员承诺。
