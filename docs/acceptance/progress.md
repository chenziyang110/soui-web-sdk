# SDK 生产验证执行记录

关联：[完整方案](../roadmap/0.3-sdk-validation.md)、[中期准入判定](interim-decision.md)、[ADR-0002](../adr/0002-public-sdk-acceptance.md)、[ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md)。开始日期：2026-09-22。完整目标持续进行，未生成正式生产准入结论。下文早期“当前候选”描述为当次实验的历史身份；preview.4 的新固定包证据在文末单列。

逐能力的公共入口、当前证据与剩余条件见[能力映射](capabilities.md)；设备和内核维护分别见[支持矩阵](support-matrix.md)、[内核准入核对](kernel-maintenance.md)。

| 要求 | 当前工作与完成证据 |
|---|---|
| 公共 SDK 独立接入 | 已新增 `tests/acceptance-app`，从独立安装的 SDK 编译；x86/x64 Release 已真实运行，见[生命周期报告](lifecycle.md) |
| V01 通信与长任务 | preview.5 的连接器及 Host 容量完成短测；待审查 PR #26 已加入公共任务服务、模板、Demo 公开任务场景和独立消费者。双架构原生契约、Demo 无窗口适配器测试与外部构建通过；真实页面和长稳仍待验收 |
| V02 数据与交互 | Workbench 已有共享万条数据的虚拟滚动、中文输入事件、动效、resize 和公共任务页面，严格类型与现代/legacy 构建通过；真实 WebView、后台、IME 与 Win7 x86 待验收 |
| V03 桌面工作台 | 待将通用注册归入公共 SDK，完成对话框/剪贴板/多屏/shell 验收 |
| V04 多窗口与驻留 | 已复现旧版第二窗口挂载卡住并修复进程级内核所有权；三窗口隔离及关闭后重建短测试通过；公共窗口/托盘/菜单/快捷键仍待完成 |
| V05 资源与故障 | 已修复逐窗口初始化导致的访问冲突，以及 ToolTip、拖拽区域和线程唤醒事件释放遗漏；preview.4 双架构四种模式千次操作通过。资源所有权、完整故障恢复与诊断仍待完成 |
| V06 接入与维护 | preview.4 的[仓库外生成项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)完成包校验、x86/x64 Release 独立编译及真实启动；[业务小样四段升级回退](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)完成本机 x64 Release 的逐段重新编译、实际启动及业务文件保留；Win7 SP1 x86 QEMU 独立消费者四模式各三轮通过。Debug、x86 升级链和真实目标设备仍待完成 |
| 每种模式 1,000 次 | preview.4 固定包的软件路径 x64/x86 重载、销毁重建、三窗口及 Bridge 各 1,000 次完成并正常退出；资源门槛仍未评估 |
| 100 次重载资源趋势 | preview.4 固定包 x64/x86 均完成预热 3 次、静置 1 秒的 100 次重载；末点 Private Bytes 分别增加约 7.0/4.4 MiB，低于 16 MiB 数值线。分段趋势与对象所有权尚未验收 |
| 8 小时混合负载 | [独立消费者混合生命周期短测](../../verification/production-validation/preview5-mixed-soak-preflight-2026-09-24/README.md)已在 x86/x64 通过；公共任务业务链、外部输入和 8 小时正式结果尚未完成，不能算生产长稳门槛通过 |
| 系统、DPI、多屏、IME | 已登记本机精确系统与驱动；[Win7 SP1 x86 QEMU 公共消费者短验收](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)完成四模式各三轮；[模板与 Workbench 短验收](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)完成三项应用启动/关闭、载荷校验及两种模式的可见界面；物理设备及[支持矩阵](support-matrix.md)中的 DPI、多屏、IME 等仍未验证 |
| 内核维护、安全、授权、发布责任 | preview.4 固定包已换入重编译内核，x86/x64 回环正负向各六项再次通过；实际信任根与代理、完整安全维护、授权及发布责任仍未关闭 |
| Electron 性能与体积对照 | 共享页面、双端最小宿主和[结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)已实现；[x64 同页面最小运行包测量](../../verification/production-validation/benchmark-size-preview5-2026-09-24/README.md)已归档完整文件清单、两份 ZIP 和各自交付目录启动探针。真实输入、呈现、进程树性能原始样本及完整业务包体积待完成 |
| 接入、兼容和维护成本 | 待通过实际小样与升级记录采集 |
| 最终交付与三个结论 | SDK、模板、Demo、测试、benchmark、支持表、迁移/诊断手册和工程/生产/价值判定均须逐项验收 |

本文件记录执行进度，不能代替机器报告或修改 `releaseReady`。新增阶段证据应链接实际 run-id、源码与载荷身份，并列出未执行项。

当前开发版本为 `0.3.0-preview.8`。根 `verification/acceptance.json` 仍是 preview.2 基础切片的历史结果；本轮新记录在 `verification/production-validation/`，不能互相替代。模板和 Demo 默认/React 的 x86/x64 Release 回归均已通过。此前 x86 完整构建有一次资源目录替换 `EPERM`；旧目录恢复后，未改源码重试通过，失败日志保留且根因未关闭。

preview.7 的[源码编码修复诊断](../../verification/production-validation/preview6-relocated-dependency-encoding-2026-09-25/README.md)确认 preview.6 无 Git 源包在 CP936 环境重编 x86 Miniblink 失败；定点 BOM 补丁后，修改过的仓库外目录里 x86/x64 SOUI、Skia、Miniblink 源码重编与两架构真实应用启动均通过。当前 preview.7 的双架构 Release 构建、契约测试和三入口 smoke 也通过，但它们使用固定运行包，不能和新内核结果合并。[Win7 SP1 x86 QEMU 重编内核短测](../../verification/production-validation/win7-sp1-x86-qemu-preview7-rebuilt-source-2026-09-25/README.md)完成三入口启动/关闭，24 份串口文件、来宾身份和载荷散列复核通过。诊断目录不是最终 preview.7 原封整包；最终整包重建、Debug、物理设备、长期资源、实际呈现性能与安全授权仍未完成，`sourceDeliveryVerified=false`、`releaseReady=false`。

[preview.7 无 Git 完整源码快照](../../verification/production-validation/preview7-full-source-stage-2026-09-25/README.md)经源清单逐文件验证，在仓库外完成 x86/x64 Release 应用重编、完整测试和三项桌面 smoke；SDK 新包的 408 个文件也通过包内完整性、源匹配及 14 项包行为测试。该快照使用固定运行依赖；最终原封整包的底层依赖全量重编继续待验收，不改变 `sourceDeliveryVerified=false`。

后续已从 SHA-256 `A4F0703A4C6F6E13DC4A07BC59174D347487622143AAB51E82B3D3F4190D802E` 的[最终 preview.7 原封完整源码 ZIP](../../verification/production-validation/preview7-final-source-bottom-rebuild-2026-09-25/README.md)分别全新解压 x86/x64 无 Git 目录。编译前两目录清单验证均通过 30,719 文件；各自重编所有底层依赖、应用 Release、完整测试和重编 DLL 启动，均退出 0。[同一 x86 来源的 Win7 SP1 x86 QEMU 三入口](../../verification/production-validation/win7-sp1-x86-qemu-preview7-final-source-2026-09-25/README.md)也通过 24 份串口结果校验。旧 SOUI 工程会在源码树产生 112 个额外编译产物，故编译后原封文件集合复验失败，但逐文件比对确认清单源码内容未改；交付流程仍需修正。Debug、目标设备、资源、实际呈现、完整业务与安全授权未通过，`sourceDeliveryVerified=false`、`releaseReady=false`。

preview.8 的[无 Git 候选完整源码包](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)在 x86/x64 全新目录完成全部底层重编后，30,794 文件清单依然通过；四组 Release/Debug 应用构建、完整测试与三入口桌面短测、双架构重编 DLL 短启动均通过。[仓库外新项目](../../verification/production-validation/preview8-isolated-source-build-2026-09-25/README.md)双架构 Release 实际运行，[独立业务项目四阶段升级/回退](../../verification/production-validation/preview8-independent-upgrade-2026-09-25/README.md)双架构通过。[同源 x86 重编载荷的 Win7 SP1 x86 QEMU 三入口](../../verification/production-validation/win7-sp1-x86-qemu-preview8-isolated-source-2026-09-25/README.md)已核验 24 份客体文件。[最终源码包生成的独立项目 Win7 SP1 x86 QEMU 短测](../../verification/production-validation/win7-sp1-x86-qemu-preview8-independent-consumer-2026-09-25/README.md) 从最终包生成模板工程、在来宾逐项校验 71 文件并正常启动/关闭；第一次脚本失败被保存，修正后复测通过。最终源码包与候选的代码等价性已核对；物理目标设备、八小时资源、Bridge/GPU、Electron 呈现和安全授权仍需收敛；`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。

ToolTip 修补后的 x64 内核及全部原生依赖已重新编译；候选 SHA-256 为 `32d37e6399bc04e2a8d909c4021f49e3b8f21521b805f9f118f15e111ea0e59f`，三模式各 30 轮加 3 次预热全部完成。重建 USER 不再增长，但 GDI/普通句柄仍增长，缺陷未关闭。随后定位到拖拽区域和工作线程唤醒事件的释放遗漏，保留为两个后续补丁。

v2 驱动对应 SDK/工具源码指纹为 `e29dc0d908c741642615b27d98f0d50d262131b10d0094d8b8c140660021027f`；它同时冻结消费者源码和实际候选内核输入，区别于历史 SDK 构建指纹。固定 prebuilt 始终未替换；候选压力验收仍待完成。

前两个补丁的 x64 候选 `980339823c0d25522c236e1b9f44fa36146a969d3aa8624f6ad63271e134dcfa` 已完成 30 轮重建：USER/GDI 无增长，普通句柄仍增加 30，见[分步报告](lifecycle.md)。三个补丁组合的 x64 候选 `2ed34e54c26fe074e020cee6d179ba2330dff7865db6fb3b75df039f4c69e565` 随后完成三模式各 30 轮；重建普通句柄保持 280，三窗口预热后到末轮仅增 1，未出现此前逐 WebView 累积。短回归不能关闭内存、线程残留和长期资源门槛。源码交付清单、固定依赖更新、PR 与完整交付尚未收敛。

x86 三补丁组合 `7ebac616b06aae2c86bc6a7775efed5afd6c86a542754e0dbe4e0179935131cc` 也已完成全依赖源码重建，以及独立消费者三模式各 30 轮；重建 USER/GDI/普通句柄增量均为 0。两架构的完整模板/Demo 与重编译依赖组合回归、候选长循环继续执行；不把短回归扩大为生产通过。

`e29dc0d908c7` 对应的两架构完整应用构建及三入口真实启动已通过，见[回归归档](../../verification/production-validation/regression-e29dc0d908c7/x64-observation.json)。重编译依赖的模板和 Workbench React 也已启动并正常退出；该旧依赖 smoke 工具仍输出 v1，只保存 DLL 散列，未达到完整载荷/源码身份的 v2 证据要求。此缺口明确保留，不能用本次短启动提前放行新固定依赖。

三补丁 x64 候选完成 100 次重载加 3 次预热、每轮静置 1 秒：末次 Private Bytes 比预热末点增加 9,846,784 字节，普通句柄由 296 到 295，USER 保持 23。数值内存增量低于 16 MiB，但资源与线程残留尚未整体验收，见[候选观察](../../verification/production-validation/96823023-6bef-437f-b623-447093d95089/reload-c4134f7c-1363-4579-ba6c-99ffcc195ada/resource-observation.json)。

候选 x64 的三模式各 1,000 次循环已开始。长循环运行在冻结副本中：复制全部 623 个 SDK 构建输入（指纹与原构建一致）、已安装库、消费者源码及已构建载荷；仍由原 v2 驱动逐次核对。开发树可继续变更，冻结副本不允许修改。该副本只供已构建候选验证，不是完整第三方源码交付包，不产生 `sourceDeliveryVerified`。长循环完成前不更新通过结论。

冻结 `e29dc0d908c7` 候选已完成三种模式各 1,000 次，均正常退出。reload/recreate/multiwindow 的 Private Bytes 末点相对预热分别增加 71,544,832、41,148,416 和 57,774,080 字节；recreate 的普通句柄/线程/USER 分别增加 18/4/5。原始记录和端点观察已归档到[生命周期报告](lifecycle.md)，整体资源门槛没有通过。这些是旧最小夹具的冻结结果，不包含新增 Web 连接器。

连接器这一批单元验证共 43 项：Web 客户端 31、Host 实际 resolver 脚本 4、监督器 8；模板类型检查和 Chrome 49 构建通过。新增原生请求预算测试及既有原生契约在 x64 执行通过。SDK/模板实际桌面集成需要另行执行，编译和模拟端口不替代该证据。

首次 v3 消费者 Web 构建在经典入口转换时失败：控制工具的工作目录与独立页面根目录不同，导致 Vite 虚拟 polyfill 的清单键发生变化。已让工具从独立页面根目录执行构建；首次失败目录 `out/acceptance/e913fb88-9df1-460a-9c8a-0f0a3044eadc` 保留，随后 `59c34fe00fee` 对应消费者准备成功。该失败属于测试构建链，未计作原生 SDK 运行缺陷。关闭准入屏障随后又有补充，最终真实运行应绑定新的完整指纹。

`f148b650e328` 的 x64 完整构建、本地契约测试以及两架构独立消费者编译通过，但实际 `bridge` 探针在两架构均失败。补充只读页面诊断后，[诊断运行](../../verification/production-validation/9cd2f5b9-5ad6-4994-9033-02bb3ff4d92a/bridge-082cf0ad-6ef2-4bc6-9378-ed2c9c216565/result.json)显示原生返回空值。根因是注入脚本的前导换行使原生包装的 `return` 触发自动分号插入；脚本原有测试未覆盖这层包装。测试改为真实包装方式后先复现失败，移除前导换行后恢复通过。修复后的原生集成继续验证，失败记录不覆盖、不计通过。页面诊断也改为共享结果所有权，避免等待超时后仍写入已离开的栈变量。

修正注入包装后，`2cf29a065477` 的公共 Web SDK 消费者在 x64 的三种生命周期短测均通过，但通信容量探针失败。[细化诊断](../../verification/production-validation/076187ed-66f1-4e0d-ae12-e48da99be5e4/bridge-59165ceb-51f4-4a86-a507-9b55fcd4b899/result.json)确认返回 2 次 `E_BUSY`，没有其他错误或错配。V8 在原生回送 lambda 退出前执行已完成请求的后续回调，旧请求因此仍占用一个槽位。已改为通知 JS 前幂等归还槽位，并测试旧回调对象析构不会释放同 ID 的新请求；容量断言仍要求只拒绝真正多出的 1 项，没有放宽。

当前 `8d75a05a2683` 的 [x64 v3 消费者](../../verification/production-validation/7865f62e-1992-4bb6-be47-733b21e4dc1f/build.json)四场景各 3 轮均正常退出：实际标准连接器、32 并发、原生 256 上限/控制容量、200 ms 等待期间页面定时器推进及错误恢复已在本范围通过。幂等预算归还的 x64 原生单元测试也通过。新 SDK 尚未完成模板/Demo 的最终整组回归和新的长循环。

同指纹的 [x86 v3 消费者](../../verification/production-validation/56fc6fee-15b9-407c-a6b1-7178c27111c7/build.json)编译成功，但四场景均在首次窗口身份调用处等待超时；页面已完成 bootstrap，停留在“检查窗口身份…”。失败记录保持不变。后续诊断确认交付动态绑定头把实际导出为 C++ `bool` 的 `jsIsString` 等 54 个函数声明成了 Win32 `BOOL`；x86 返回 ABI 不一致把 Promise 误判为字符串，请求因此未入队。直接改用 `jsTypeOf` 的反证实验访问冲突，最终按内核 `wkedefine.h` 对齐全部共享符号，并增加逐符号契约测试。[根因归档](../../verification/production-validation/x86-bool-abi-root-cause/README.md)保存修复前、反证和修复后的运行及 trace。

当前源码指纹 `3dcd7b97ea5f75deb752b1940ac34a30875eba6e851b00603ea426e88e1da699` 的 [x86](../../verification/production-validation/468fb9ac-9426-43e6-a878-19135c469d3e/build.json) 与 [x64](../../verification/production-validation/ad180ad6-6287-4966-930e-5223fb7378f2/build.json) 公共消费者均完成 reload、recreate、multiwindow、bridge 各 3 轮，全部正常退出。x86 的修复后 trace 已确认 resolver 创建、原生队列、工作线程响应及 JS Promise resolve 完整贯通。该结果关闭当前短回归的架构差异，不关闭 1,000 次、8 小时、资源趋势、平台矩阵、桌面模块及 Electron 对照门槛，`releaseReady` 保持 false。

随后完整应用 smoke 暴露模板和 Workbench 默认开启 GPU compositor 时的关闭崩溃：x64 三个入口都在 `adapter-events-drained` 之后、`adapter-webview-destroyed` 之前以 `0xC0000005` 退出；对已通过的独立消费者单独设置 `SOUI_GPU_COMPOSITOR=1` 也能复现，direct 为 1 或 0 均失败。[失败归档](../../verification/production-validation/regression-3dcd7b97ea5f/x64-gpu-default-failure/smoke-Release.json)保持原始退出码。模板及 Workbench 默认改为稳定软件路径后，源码指纹 `6649c3929622` 的 [x86/x64 回归](../../verification/production-validation/regression-6649c3929622/observation.json)均完成 Starter、Workbench 默认页、Workbench React 页三入口并正常关闭。GPU 模式继续作为实验缺陷，不计入生产通过。

GPU 专项继续验证了公共 SDK 消费者的 x64 重建 30 轮：固定内核及三项资源修补候选均正常退出，但 Private Bytes、USER 与普通句柄持续增长。把 GPU 表面移到合成线程析构并补齐 Skia 接口引用释放后，30 轮仍增加约 75.9 MiB Private Bytes、30 个 USER 对象和 206 个普通句柄；WGL 删除与 DC 释放在这组样本中均返回成功。[逐候选原始证据和限制](gpu-lifecycle.md)已留存。旧 GPU 关闭访问冲突不因当前短测未复现而关闭；GPU 资源门槛明确未通过，固定 prebuilt 不更新，`releaseReady` 仍为 false。

同一候选的 x86 GPU 30 轮也出现 USER 每轮约增 1；x64 软件路径 30 轮虽有句柄与进程内内存上升，但随后 100 轮显示周期性回收，不能误判为持续线性泄漏。最小 WGL 探针同机 33 轮 USER/句柄稳定，完整路径的 GPU 剩余对象所有权未定位，见[专项原始样本](gpu-lifecycle.md)。当前源码指纹 `b565bd098865` 的 [x86/x64 完整应用回归](../../verification/production-validation/regression-b565bd098865/)均通过构建、单元/契约及三个真实 smoke 入口；x64 首次构建又遇资源目录改名 `EPERM`，失败日志保留，未改源码重试通过。新诊断和短回归均不改变 preview/生产判定。

当前标准连接器的软件路径 [x64 重建 1,000 轮](lifecycle.md#当前标准连接器的-1000-次重建)与 [x86 重建 1,000 轮](lifecycle.md#x86-当前连接器的-1000-次重建)均已完整结束并正常退出；每百轮中位数和关闭后资源另行记录，两边 `resourceGate` 均为 `not-evaluated`。这只关闭该候选两个架构的重建操作门，不替代其他模式、资源或 8 小时业务门槛。

同一 x64 冻结载荷的 [100 次重载](lifecycle.md#当前连接器的-100-次重载)也完成：每轮静置 1 秒，末点 Private Bytes 较预热增加约 6.0 MiB，低于 16 MiB 数值线；USER 保持不变，句柄仅增 5，分段样本有回收。`resourceGate` 仍未评估，不把这项数值观察扩大成整体资源或生产通过。

当前 x86 冻结载荷的 [100 次重载](lifecycle.md#x86-当前连接器的-100-次重载)也完成：同样每轮静置 1 秒，Private Bytes 末点增加约 7.2 MiB，低于 16 MiB 数值线；普通句柄增加 47、USER 增加 2，Runtime 关闭后仍有 284 个普通句柄与 6 个 USER 对象。数值线通过不等于资源所有权解释完成，`resourceGate` 仍未评估。

当前连接器的 x64/x86 [1,000 次重载循环](lifecycle.md#当前连接器的-1000-次重载循环)也都正常退出；20 ms 静置下内存与句柄大幅波动并有回收，不用它替代 1 秒静置的 100 次资源数值观察。当前两个架构的重建与重载操作门已覆盖，其他模式、GPU 和资源门仍未关闭。

当前三个 JavaScript 锁文件分别执行了 [SDK](../../verification/production-validation/regression-b565bd098865/npm-audit-sdk-web.json)、[模板](../../verification/production-validation/regression-b565bd098865/npm-audit-starter-web.json)和[Workbench](../../verification/production-validation/regression-b565bd098865/npm-audit-workbench-web.json)的 `npm audit`，报告均为 0 条。这个结果仅覆盖 npm 依赖，不替代内核、TLS、授权及发布审查。

当前包的[仓库外生成项目](../../verification/production-validation/independent-project-b565/README.md)已分别完成 x64/x86 Release 编译、真实启动和正常关闭，包清单、项目锁、构建身份及原始启动日志均已归档。这证明模板不靠原仓库直接链接运行；仍未完成业务小样与升级/回退运行链。

[HTTPS 身份校验候选](../../verification/production-validation/tls-kernel-2026-09-23/README.md)在 x86/x64 各完成六项本地正负向测试，重编译 DLL 与公共消费者 Bridge 短回归绑定。旧固定 prebuilt 尚未替换；系统信任根、代理、动态子资源和旧 TLS 组件维护没有因该小范围测试而通过。

随后按 [ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md) 将相同散列的双架构候选 DLL 放入 `0.3.0-preview.4` 固定包。[新包回归](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)绑定 x86/x64 完整 Release 构建、SDK/Web/原生契约、模板与 Workbench 三入口真实 smoke、独立公共消费者四模式各三轮，以及固定 DLL 的回环 HTTPS 各六项测试，均通过。新包[仓库外项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)通过包/锁校验、双架构 Release 编译和真实启动关闭。旧 preview.3 千次与 100 次资源结果仅作历史比较；preview.4 的长循环在下文单独列出，完整资源和 8 小时门槛未通过，`releaseReady` 保持 false。

[preview.4 完整源码包搬移实验](../../verification/production-validation/preview4-source-2026-09-23/README.md)在无 Git 的仓库外解压目录通过逐文件校验、x64/x86 Release 完整应用编译与三个入口启动关闭；这是当次源码快照的证据。最终源码清单和 ZIP 必须匹配纳入新证据后的提交；底层 Miniblink/SOUI 在搬移目录的源码重编译仍待执行。

preview.4 固定包的[新生命周期归档](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)已保存 x64/x86 各 100 次重载及四种模式各 1,000 次，均正常退出且无监督超时。100 次重载 Private Bytes 末点相对预热增加 7,368,704 / 4,648,960 字节，分别低于 16 MiB 数值线；20 ms 高频运行的峰值和句柄明显更高，不用它代替资源验收。各次 `resourceGate` 均为 `not-evaluated`。为使后续长测在开发树变化时仍绑定同一载荷，已另建隔离副本并核对两架构源码指纹、SDK build-id 及公共消费者身份；该副本仅服务测试，不等同完整第三方源码交付。8 小时混合业务仍需执行。

同一 preview.4 冻结 x64 载荷的[三窗口 1,000 轮](../../verification/production-validation/preview4-lifecycle-2026-09-23/x64-1000-multiwindow/observation.json)也完成并正常退出、无监督超时；预热末点到末轮 Private Bytes 由 82,006,016 增至 148,246,528 字节，普通句柄由 408 增至 483。逐百轮中位数后段高于前段，`resourceGate` 仍为 `not-evaluated`。该结果只增加多窗口操作证据，不能当作资源回收或业务长稳通过。

相同冻结源指纹的[x86 三窗口 1,000 轮](../../verification/production-validation/preview4-lifecycle-2026-09-23/x86-1000-multiwindow/observation.json)亦正常退出且无监督超时；Private Bytes 由 57,827,328 增至 116,793,344 字节，普通句柄由 406 增至 781。分段内存中位数总体抬高，资源所有权未解释，`resourceGate` 保持 `not-evaluated`。当前开发机运行 x86 进程不等于在目标 32 位或旧版 Windows 上验收。

[x64 Bridge 千次](../../verification/production-validation/preview4-lifecycle-2026-09-23/x64-1000-bridge/observation.json)和[x86 Bridge 千次](../../verification/production-validation/preview4-lifecycle-2026-09-23/x86-1000-bridge/observation.json)也在同一冻结身份上正常退出、无超时；预热末点到末轮 Private Bytes 分别增加 47,562,752 和 54,177,792 字节，分段中位数总体抬高。它们证明公开连接器循环操作和关闭路径，不证明内存回收、真实业务长任务、任务背压或 Win7 SP1 x86 兼容。

共享性能页面及双端最小宿主已落到 [`benchmarks/`](../../benchmarks/README.md)；页面 TypeScript 检查与构建、Electron 精确版本 `44.4.4` 安装/语法检查、公开 SDK 的 x86/x64 C++ 宿主编译已执行。[结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)实际运行了两架构 SOUI 和 Electron，三份 S01 输出可校验，但自动完成探针没有真实交互、帧样本或完整采样区间，不能作为同机同负载性能对照。

[独立业务项目四段升级回退](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)在仓库外完成 preview.3 → preview.4 → preview.3 → preview.4，每段锁校验、x64 Release 重新编译、实际启动/关闭及业务标记均通过；五个业务文件散列不变，四个 build-id 与载荷身份已交叉核对。第一次尝试误用未随生成项目交付的 `tools/Test.ps1`，首段编译虽通过但无有效启动结论；失败日志保存，全新目录修正验证驱动并重跑四段。该证据不覆盖 Debug、x86 升级链、安装签名及目标机。

[Windows 7 SP1 x86 QEMU 目标系统短验收](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)使用相同 preview.4 冻结源码指纹的独立公共消费者，在 6.1.7601/32-bit 上完成 Bridge、重载、销毁重建、三窗口各三轮，退出码 0，串口导出 17 个文件的长度和散列均通过，并验证每个事件流以 `run.complete` 结束。初次 QEMU 共享 FAT 盘报告截断后，所有有效结果均在来宾 NTFS 上重新运行；短验收只说明软件路径启动与基本生命周期可工作，不覆盖物理设备、资源门槛、真实业务、IME/DPI、TLS/代理、安装升级或 8 小时。

[同一 Win7 SP1 x86 虚拟机的模板与 Workbench 短验收](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)进一步使用同一冻结源码指纹的 Release 载荷。模板及 Workbench 默认/React 三项烟测均退出 0，日志确认页面和桥接就绪、关闭完成；两个 ZIP、541 个载荷文件、中文文件名和四个关键二进制散列经来宾校验，24 个串口导出文件经宿主重验。正常模式下的默认/React 界面已留截图。该结果尚不足以宣布 Demo V01–V06 或指定平台的生产通过。

preview.5 的[任务容量修复证据](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)绑定源码指纹 `16d1dccc47ebacff83bf0a94abeed21c75cd7bcd049559802598a5e44656f237`：x86/x64 Release 完整构建、三项实际应用 smoke、原生任务容量/并发/取消/扫描错误传播测试均通过；由当前 SDK 包生成的仓库外项目也在双架构完成 Release 编译与实际启动/关闭。无 Git 完整源码解压目录经逐文件验证后也在两架构重新完成应用构建、测试和三项 smoke。三个 Web 锁文件本轮 `npm audit` 均为 0 项。打包发布目录的 Windows 短暂 `EPERM/EACCES` 有界重试经过注入测试与本机反复打包；实际锁持有者仍未确认。[preview.5 Win7 SP1 x86 QEMU 应用短验收](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)进一步核对 24 个串口导出文件，模板与 Workbench 默认/React 三入口均退出 0；preview.4 的四模式公共消费者及长循环不能升级为 preview.5 的通过结论，完整业务链和资源门槛继续待验收。

[preview.5 Win7 SP1 x86 QEMU 公共消费者四模式短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)随后使用独立安装的 SDK 与公共 Web 连接器重新编译，在 6.1.7601/32-bit 来宾分别完成 Bridge、重载、销毁重建、三窗口各 3 轮，退出码均为 0。26 个串口文件的长度与 SHA-256、来宾身份、运行载荷、完整事件步骤及资源采样顺序经宿主复核。该结果只关闭新版的四模式短操作门；任务容量满载业务、千次循环、资源所有权与 8 小时混合负载尚未通过。

preview.5 固定载荷的[x86 Bridge 长循环复测](../../verification/production-validation/preview5-lifecycle-2026-09-24/README.md)有两次千轮目标失败、三次同载荷诊断成功；失败时未抓到线程栈。为缩小下次复现范围，公共验收消费者增加可选的挂载阶段日志。另一源码指纹的[x86 1,000 轮阶段诊断](../../verification/production-validation/preview5-mount-phase-2026-09-24/README.md)完成全部操作且无超时，10,030 条阶段事件和观察器报告均归档；它不替代原载荷失败样本。生产零超时、内存/句柄所有权及 Win7 客体长稳仍未通过。

随后监督器增加[超时现场保存](../../verification/production-validation/preview5-timeout-diagnostics-2026-09-24/README.md)：记录已读日志偏移、未读内容和最后原生事件；配置 x86 调试器时，在终止进程前尝试抓所有线程栈及完整转储。受控进程的抓取测试通过。该新来源的 x86 Bridge 长测连续两次完成 1,000 轮，均未触发故障抓取，因此原两次超时的根因和生产门槛没有改变。

原失败源码在独立目录重新编译后又完成一次 x86 Bridge 1,000 轮，外部 8 秒观察器未见停滞；新构建载荷 SHA-256 与失败载荷不同，不能合并为原载荷成功样本。原失败保持两个，故障线程栈仍缺失。

[preview.5 新诊断源码的 Win7 SP1 x86 QEMU 100 次重载](../../verification/production-validation/win7-sp1-x86-qemu-preview5-reload-2026-09-24/README.md)进一步在来宾 NTFS 完成 3 次预热 + 100 次测量、109 个生命周期步骤，退出码 0；15 个串口文件及系统/二进制身份经宿主复核。Private Bytes 预热末点到最后一轮增量 14,876,672 字节，低于 16 MiB 数值线，但句柄与线程分别增加 57、4，资源门仍未评估。这是虚拟机与不同源码指纹的重载证据，不能替代原 Bridge 失败定位或物理设备验收。

[preview.5 原生任务容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)进一步在 Win7 SP1 x86 QEMU 客体执行：来宾 6.1.7601/32-bit/SP1 身份、测试 EXE SHA-256、退出码 0 与标准输出由五个串口文件逐项复核。它覆盖原生任务表 1,024 上限、取消回收、并发准入和扫描拒绝；首次临时载荷缺 DLL 的加载失败已保留并在补齐后重跑。公共任务业务链、长稳与物理目标机仍未通过。

[preview.5 最小宿主包体积测量](../../verification/production-validation/benchmark-size-preview5-2026-09-24/README.md)把同一份页面装入 x64 Release SOUI 和 Electron 44.4.4 的独立运行目录，逐文件核对散列，使用相同 ZIP 算法并测试压缩包。SOUI/Electron 解压大小为 38,765,828/386,055,337 字节，ZIP 为 14,533,916/154,312,842 字节。两端从打包目录执行 S01 结果落盘探针并退出 0。探针不含真实交互或呈现，Electron 标准错误有 GPU 状态告警；该测量只支持最小宿主体积差异，不能作为性能或生产通过结论。

公共 SDK 长任务服务开发分支新增已认证任务启动、查询、订阅、协作取消与有界工作队列，并在模板接入可取消计数任务。x86/x64 Release 原生任务契约、模板 x86/x64 编译、模板 Chrome 49 页面构建、TypeScript 检查和 Web 客户端单元测试通过；这些是开发树验证，不属于 preview.5 冻结包。真实页面操作、独立安装工程、Demo V01、Win7 SP1 x86 客体及长时负载尚未对这份新源码执行，`releaseReady` 保持 false。宿主 preview.5 的 8 小时诊断与已准备的客体混合场景也仍分别按原载荷身份判定，不合并为新功能的通过结果。

独立验收消费者已增加真实公共任务处理器和页面进度/取消探针，x64 与 x86 均在 `-PrepareOnly` 路径完成安装 SDK 后的外部原生编译、Chrome 49 页面构建及运行载荷身份核对；该路径不启动桌面窗口。首次 x86 准备在 F 盘空间耗尽时失败，保留失败 suite，随后将 20 份旧验收 suite 共约 1.77 GB 完整移到 D 盘归档并重跑准备成功。新探针实际运行及客体回归待宿主长测结束后串行执行，不把编译成功计作功能通过。

外部消费者构建进一步加入固定 TypeScript 5.5.4 的严格检查，对复制到 suite 的 SDK 声明文件解析，并将检查配置/日志散列写入 Web 构建记录，由监督器运行前复核。x64 与 x86 新 suite 的检查、Chrome 49 构建、外部 C++ 编译与载荷准备均通过；尚未启动桌面场景。

Demo 开发树现用公开 `task.start` 接入一个有界计数任务，并以独立前缀路由公开任务的查询、订阅、取消和会话回收。x86/x64 Release 编译和 `demo.native.public-task` 无窗口适配器测试通过；React 页面构建和项目内固定 TypeScript 5.9.3 严格检查通过。原有 TypeScript 5.5.4 无法解析当前 Vite 插件声明中的字符串模块导出，故此处只升级 Demo Web 的编译器锁定版本，独立消费者仍按自身的 5.5.4 锁文件检查。Demo 真实 WebView 页面未运行，新公共任务不能据此宣称完成 Win7 或生产验收。

双端 benchmark 的历史落盘探针复核发现内容区不一致：Electron 为 1100×720，SOUI 页面实际为 1084×681。SOUI 最小宿主已改为按内容区创建窗口，并加入创建后的尺寸检查；页面结果前置校验器会拒绝旧探针的视口失配、短时长及空操作。新宿主尚未实际运行，校验器只检查页面侧基础条件；外部输入、物理呈现和进程树资源仍未采集，Electron 性能结论继续未测。

双架构 benchmark 宿主已在开发树重新编译；新页面结果校验器的三项测试通过。独立 Win32 进程树采样器还完成 x86/x64 编译，并在 x64 主机用真实父子进程、父进程先退出的后代、正常退出、时长超限和输出冲突验证原始 NDJSON、退出码及追踪行为。它未在正式 SOUI/Electron 轮次运行，物理呈现和配对报告也未完成，因此资源与流畅性仍未测。

外部 Win32 输入轨迹工具已完成 x86/x64 编译与拒绝无效 PID、场景、输出路径和模式的边界测试。它按相同内容区坐标通过系统输入队列注入点击、滚轮与 Unicode 键，并记录 QPC；正式宿主尚未执行该工具，S03 的 Unicode 注入也不等于中文 IME composition。页面事件/动作、外部输入轨迹及实际呈现三者尚未形成可用的配对轮次。

新增逐轮原始轨迹复核器，将外部输入与整进程树采样按 PID、创建时间及采样区间关联，拒绝短探针、采样长缺口、宿主异常退出和未采到真实 IME composition 的 S03。合成正常/故障轨迹单元测试通过；当前仍无真实宿主的双轨迹轮次，更无物理呈现或完整 Electron 配对数据，性能和生产门槛继续未测。

宿主 preview.5 八小时混合测试运行期间，2026-09-25 02:33 本机出现另一台独立 QEMU 虚拟机，约 4.9 GB 工作集；该时点以后的宿主资源趋势不能当作隔离环境结论。原始测试继续运行并在其外层目录保留外部负载观察记录。两个待执行的 Win7 验收启动器增加对 x86/x64 QEMU 进程的互斥检查；宿主终态及其他虚拟机退出前不启动该目标客体。

同一次测试又在 02:39 和 03:26 观察到另外的 x64 QEMU 启动，逐次进程/时间/内存观察已归档于[外部负载记录](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/README.md)。这些观察不证明完整干扰区间，但足以使重叠时段的隔离资源与时延结论失效；原生操作和最终退出仍按原始长测结果单独判定。

后续在 04:41 和 04:52 又观察到两台独立 x64 QEMU，04:53 同时运行；其进程身份、启动时间和内存快照追加到同一[外部负载记录](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/README.md)。本仓库在长测期间还进行了增量构建和审计；隔离资源/时延门槛须另起完整运行，本次只保留操作完成率与终态证据。

本地 `tools/Test.ps1` 的非桌面测试阶段现纳入页面配对与逐轮原始轨迹核验测试；连同已有 Web、验收监督器测试实际执行 57 项，56 项通过、1 项因未配置 x86 调试器而跳过，0 失败。该命令未启动桌面宿主，不能代替真实双端交互或客体验收。

V03 公共桌面服务的路径 Unicode 转换在源码审查中发现输出缓冲区少计了终止符，选择中文目录时可能得到错误文本；公共转换现使用精确输入长度和所需输出长度，标题/剪贴板写入拒绝无效 UTF-8 与嵌入 NUL。x86/x64 Release 的 SDK 静态库和新增原生边界测试已分别编译、通过；目录选择器真实 UI、Win7 客体、多屏与服务注册仍未验收。

V03 剪贴板进一步发现原实现以空窗口所有者打开剪贴板，按 Win32 规则会使 `SetClipboardData` 失败。公共服务现要求同进程有效窗口，Workbench 传入其宿主窗口；x86/x64 SDK 与 Workbench Release 重新编译，通过空/无效窗口及无效文本拒绝测试。实际中文写入/读回的可选测试已加入测试程序，须等宿主长测结束后在隔离的 Win7 x86 客体执行，尚未将该服务计为可用公共 Web 模块。

公共剪贴板写入现可由新项目在首次 bootstrap 前显式启用；模板项目包含实际点击调用及独立类型声明。x86/x64 的 SDK 契约和模板 Release 编译、SDK Web 构建、模板类型检查及 Web 构建均通过；完整非桌面测试 57 项 JavaScript 中 56 通过、1 项因缺 x86 调试器跳过，x86/x64 原生各 6 项通过。已在仓库外安装两个架构 SDK，独立消费者只通过安装包公开头文件与库启用剪贴板模块，分别编译和退出 0。契约确认 JSON 假手势不能替代原生手势、无手势和无窗口均有明确拒绝；尚未运行真实页面按钮与 Win7 客体读回，所以 V03-03 仍为“已实现待验收”。为恢复构建空间，将历史 `out/source-snapshot-03-final` 57,599 个文件、1,318,952,268 字节原样搬移到 `D:\SOUI-SDK-QEMU-validation\host-out-archive-20260925\source-snapshot-03-final`；源目录不再存在。该磁盘迁移发生在宿主 8 小时测试期间，相关时间段同样不用于隔离资源/时延结论。

公共窗口模块现可在新项目中显式启用，模板提供标题、最大化/还原和最小化按钮，并在 HWND 所属线程处理窗口命令。x86/x64 的 SDK 与模板 Release 编译、公共契约及桌面文字边界测试、SDK Web 构建和模板类型检查/构建通过；仓库外安装的两个架构 SDK 也由独立消费者启用模块、编译并退出 0。契约覆盖假手势、缺手势、缺少窗口和无效参数，但不替代真实 GUI 测试。当前开发树已将跨线程 `setTitle` 改为数字请求 ID 投递与 250 ms 有界确认；双架构 SDK、模板和 Demo 编译及各六项无窗口原生测试通过，窗口线程停滞回归仅完成编译，宿主长测期间不运行该 GUI 场景。若处理已开始，超时仍可能对应未知最终结果；关闭投递也不保证 Promise 在页面销毁前确认，因此 V03-01 保持待验收，细节见 [ADR-0010](../adr/0010-public-window-bridge.md)。

为目标系统实测另加入可选 `--desktop-roundtrip` 原生场景：在来宾创建真实 HWND，尝试公共标题、最小化、最大化/还原和关闭，并与剪贴板中文写入读回一起验证。x86/x64 已编译该场景；宿主长测和其他 QEMU 占用期间没有执行可选 GUI 路径，故尚无通过结论。

V03 目录选择器现作为可选公共 Web 模块注册，模板提供真实点击入口，返回用户选择的 UTF-8 路径或取消状态。SDK 原生服务在弹窗前核对同进程有效窗口和 STA，拒绝 MTA 调用；宿主 Bridge 工作线程原本已以 STA 初始化。x86/x64 SDK、模板 Release、Web 类型检查/构建及各六项无窗口 CTest 均通过，包括伪造手势、无效参数、缺失窗口和 MTA 拒绝。安装后的两个架构 SDK 还分别由仓库外独立消费者只用公开头文件和库编译、运行并退出 0，产物散列与 CTest 原始日志见[目录模块记录](../../verification/production-validation/directory-module-2026-09-25/README.md)。当前没有打开真实对话框，且模态调用会占用 Bridge 工作线程；Win7 x86、取消/中文路径、受限账户、导航/关窗竞态和 Demo 扫描令牌隔离仍待验收，V03 不据此放行。设计见 [ADR-0011](../adr/0011-public-directory-dialog.md)。

为目录模块另编译了 x86/x64 可选 `soui_directory_dialog_tests --cancel`：真实 HWND 由 UI 线程持有，独立 STA 线程经公共 `json_bridge` 打开 Shell 对话框，观察到对话框后发送关闭并核对取消返回；15 秒无终态时进程失败退出，防止客体脚本无限挂住。该可选 GUI 场景在宿主长测期间只完成编译，尚无实际运行证据；正向中文目录选择仍待模板和 Win7 客体验证。

上述 x86 可选程序已冻结为[Win7 SP1 x86 目录对话框客体短测](../../verification/production-validation/win7-sp1-x86-qemu-directory-2026-09-25/README.md)，传输散列、脚本语法、串口校验合成样本和宿主未结束时拒绝启动均已核对。真实客体尚未运行；它仅覆盖取消路径，不替代模板实际点击或正向中文路径验收。

V02 Workbench 现提供可从首页和菜单进入的数据交互页，复用 Electron 对照页的确定性万条数据生成器，组合虚拟滚动、中文输入事件、动效、窗口尺寸和公共 SDK 长任务入口。严格 TypeScript、legacy 与普通 Web 构建均退出 0，原始日志及产物散列见[编译预检](../../verification/production-validation/v02-workbench-preflight-2026-09-25/README.md)；宿主八小时长测期间没有执行另一场 GUI，因此实际滚动、IME、缩放、任务并行和 Win7 x86 仍待验收，不能把编译算作 V02 通过。

最新工作树上的[双架构非桌面回归](../../verification/production-validation/latest-unit-regression-2026-09-25/README.md)各有 9 项包/交付身份测试通过、59 项 Web/协议测试中 58 项通过且 1 项因调试器缺失跳过、6 项 CTest 通过，均为 0 失败。原生二进制沿用既有构建，本轮未重编译；未运行桌面或 Win7 客体，因此仅记录回归范围内的通过，不改变生产准入状态。

05:54 宿主八小时测试仍在执行第 1,300 轮之后的混合循环，另有外部 x64 QEMU 正运行，约 5.0 GB 工作集。[第七次外部负载观察](../../verification/production-validation/preview5-mixed-soak-interference-2026-09-25/external-load-observation-7.json)已保留精确进程身份和时间；该长测只能继续用于操作完成率与终态判定，隔离资源及性能门槛仍须重新运行。

[固定内核的源码声明材料清单](../../verification/production-validation/kernel-notice-inventory-2026-09-25/README.md)现补充定位到 libxml、libpng 和 Node c-ares 的仓内文本，同时确认导入的 libcurl、libjpeg、OTS 头文件指向本次导入源码缺少的配套许可文件。此发现只缩小再分发审查的待办范围；实际链接成员、缺失文本的精确来源、运行包声明和维护者签署仍未完成，二进制授权状态维持 `unverified`。

又从 curl 官方 7.61.1 标签和 IJG 官方 9a 压缩包[补录](../../verification/production-validation/kernel-notice-recovery-2026-09-25/README.md)两份原版配套文本，并保留原始散列及导入头文件的差异核对。OTS 等剩余声明、运行包实际链接组成及维护者审阅仍未完成，不能把源码文本补齐等同于二进制授权通过。

2026-09-25 通过仓库所有者的 GitHub CLI [查询](../../verification/production-validation/repository-visibility-2026-09-25/observation.json)到 `chenziyang110/soui-web-sdk` 的可见性仍为 `PRIVATE`。[首方许可状态](../../LICENSE.md)也尚未选定；若以后公开，公开可见仍不等于获得 SDK/模板/Demo 的通用复用或再分发许可。生产交付及对外采用结论须把这一所有者决策列为独立发布门槛，不能由第三方声明文本的补录代替。

导入的 OTS `ots.cc` 与官方 v5.0.0 仅有少量 Miniblink 适配差异；相应 [OTS 原版 `LICENSE` 已补录](../../verification/production-validation/kernel-notice-recovery-2026-09-25/README.md)。三份新增文本仍未完成整包授权审阅；`releaseReady=false`。

[第三方声明材料交付路径](../../verification/production-validation/license-delivery-2026-09-25/README.md)已补齐并在 preview.6 完整构建后复核：源码 SDK 包和安装项目、双架构 CMake 安装目录、Starter/Workbench 双架构输出均保留同一份声明与四份许可文本，32 份许可副本的原始 SHA-256 均一致。交付身份证明也纳入许可目录及声明文件，防止改动后沿用旧构建记录。此项测试没有运行应用或审完实际链接成员；运行包声明仍不完整，不能将其作为生产授权通过。

当前 preview.6 的 x86 Starter/Workbench [Win7 SP1 QEMU 短测输入](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-2026-09-25/README.md)已冻结：从现行构建载荷重新压缩并解压核对 71/475 个文件，宿主运行来宾检查脚本确认两套法务文件哈希与中文路径，串口校验器合成成功/篡改样本自检通过。宿主八小时长测和其他 QEMU 尚占用，客体未执行；不能据此填写 preview.6 的 Win7 支持结论。

preview.6 [独立安装消费者预检](../../verification/production-validation/preview6-independent-consumer-2026-09-25/README.md)在 x86/x64 均通过：`find_package(SouiSdk)` 的外部控制台工程只链接安装后的公开库并退出 0；`Test-Acceptance -PrepareOnly` 又从本版源码独立编译 SDK、安装包、TypeScript 页面和桌面消费者，记录各自 32 文件安装快照与 75 文件载荷。两套安装库与各自 SDK-only 构建字节散列一致，第三方声明和四份许可进入安装目录。此项只通过构建与无窗口调用，不增加真实桌面、Win7 或长稳通过项；`releaseReady=false`。

[preview.5 x86 八小时混合生命周期](../../verification/production-validation/preview5-eight-hour-soak-2026-09-25/README.md)已经正式结束：单进程 1,690 轮、23,664 步、28,802,459 ms，监督器判定退出 0、无超时、操作完整。分时分析同时发现私有内存中位数由首小时约 64.1 MiB 升至第八小时约 136.7 MiB；运行与外部 QEMU 及本机编译交叠，资源和时延门槛均不能用本次数据放行。应定位增长来源，在隔离条件下用最终候选重跑；此结果也不能跨版本计入 preview.6。

preview.6 的[完整源码搬移](../../verification/production-validation/preview6-source-relocation-2026-09-25/README.md)已在无 Git 元数据的仓库外目录重新校验 30,190 个文件、打出一致的 408 文件 SDK 包，并由该包生成独立项目。完整源码与生成项目的 x86/x64 Release Web/原生编译均通过；搬移源码两架构非桌面测试通过。底层依赖未从搬移目录重编，两个项目均未在该目录启动 GUI，因此 `sourceDeliveryVerified` 仍为 false。

preview.6 [Win7 x86 应用短测 v3](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-2026-09-25/README.md)已进入客体并验证两个 ZIP 的解包、数量和中文路径，但来宾 PowerShell 2 在许可散列校验器的 `SHA256Managed.Dispose()` 报错，三项应用 smoke 没有执行；串口校验器正确拒绝仅 14/24 文件的失败帧。原样本保留。[v4 独立客体](../../verification/production-validation/win7-sp1-x86-qemu-preview6-apps-v4-2026-09-25/README.md)只修正脚本清理调用，冻结应用载荷不变；实际 Win7 SP1 x86 短测的 24 个串口文件、法务材料散列及 Starter/Workbench 默认/React 三项启动和关闭均通过，退出码 0。该短测不覆盖真实交互、物理设备或长期资源门槛。

[Win7 SP1 x86 公共任务短测](../../verification/production-validation/win7-sp1-x86-qemu-public-task-2026-09-25/README.md)也已在冻结独立消费者上完成：14 个串口文件、系统/二进制身份、Bridge 三轮与 10 个完整步骤均通过，退出码 0；资源门槛仍未评估。随后[公共桌面服务短测 v1](../../verification/production-validation/win7-sp1-x86-qemu-desktop-2026-09-25/README.md)因测试 EXE 未附带 `MSVCP140.dll` 在进程加载前失败，客体退出码 `0xC0000135`；原样本保留。保持 EXE 字节不变、锁定 preview.6 应用本地 VC/UCRT 运行库的[v2 客体复测](../../verification/production-validation/win7-sp1-x86-qemu-desktop-v2-2026-09-25/README.md)已通过：真实 Win7 SP1 x86 上公共剪贴板中文写入读回和窗口标题中文往返退出 0，串口五份原始文件、系统/EXE 身份及哈希均通过独立校验。该短测仍不覆盖模板真实按钮、占用竞态、受限账户或完整 V03。

[窗口线程停滞 v2 短测](../../verification/production-validation/win7-sp1-x86-qemu-title-timeout-v2-2026-09-25/README.md)保持原候选 EXE 身份，额外锁定上述 52 个应用本地运行库，在实际 Win7 SP1 x86 QEMU 上验证超时返回、未执行命令撤销及恢复后的中文标题设置，客体退出 0。五份串口文件、系统/EXE 身份及哈希均通过校验；此结果不跨候选合并到 preview.6，也不放行模板页面或生产资源门槛。

[目录对话框取消 v2 短测](../../verification/production-validation/win7-sp1-x86-qemu-directory-v2-2026-09-25/README.md)同样保持原候选 EXE 身份、补入冻结的 52 个运行库，在真实 Win7 SP1 x86 QEMU 中打开并取消 Shell 目录对话框，退出 0。五份串口结果的系统/EXE 身份、逐文件散列及成功标记通过独立校验；快照客机已退出。它只证明公共取消路径，不涵盖模板真实点击、正向中文路径、关窗竞态或完整生产验收。

preview.8 [最终完整源码包的独立公共 SDK 页面消费者](../../verification/production-validation/win7-sp1-x86-qemu-preview8-public-task-2026-09-25/README.md)先在本机完成 x86 Release Bridge 三轮，再以同一 76 文件载荷进入 Win7 SP1 x86 QEMU。客体逐文件散列、进程退出 0、17 份串口文件、10 个完整生命周期步骤、3 轮资源采样以及页面的公开任务进度/取消、容量与错误恢复均通过独立核验；伪造内核散列和截断事件流的反例被拒绝。此结果不代表 Demo 页面真实操作或生产资源门槛，虚拟机历时也不作性能对标。
