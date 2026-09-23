# SDK 生产验证执行记录

关联：[完整方案](../roadmap/0.3-sdk-validation.md)、[中期准入判定](interim-decision.md)、[ADR-0002](../adr/0002-public-sdk-acceptance.md)、[ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md)。开始日期：2026-09-22。完整目标持续进行，未生成正式生产准入结论。下文早期“当前候选”描述为当次实验的历史身份；preview.4 的新固定包证据在文末单列。

逐能力的公共入口、当前证据与剩余条件见[能力映射](capabilities.md)；设备和内核维护分别见[支持矩阵](support-matrix.md)、[内核准入核对](kernel-maintenance.md)。

| 要求 | 当前工作与完成证据 |
|---|---|
| 公共 SDK 独立接入 | 已新增 `tests/acceptance-app`，从独立安装的 SDK 编译；x86/x64 Release 已真实运行，见[生命周期报告](lifecycle.md) |
| V01 通信与长任务 | 标准连接器、模板接入、本地请求/订阅清理及 Host 容量已实现并通过首批单元/编译；独立 v3 公共 Web SDK 消费者正在集成，公共任务服务/事件/取消仍待完成 |
| V02 数据与交互 | 待完成共享万条列表、输入、动画、resize、后台场景 |
| V03 桌面工作台 | 待将通用注册归入公共 SDK，完成对话框/剪贴板/多屏/shell 验收 |
| V04 多窗口与驻留 | 已复现旧版第二窗口挂载卡住并修复进程级内核所有权；三窗口隔离及关闭后重建短测试通过；公共窗口/托盘/菜单/快捷键仍待完成 |
| V05 资源与故障 | 已修复逐窗口初始化导致的访问冲突；长循环进一步暴露 ToolTip、拖拽区域和线程唤醒事件三处释放遗漏。三补丁 x64 组合短回归通过，重建 30 轮的 USER/GDI/普通句柄增量均为 0；长循环与完整故障恢复、诊断仍待完成 |
| V06 接入与维护 | preview.4 的[仓库外生成项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)完成包校验、x86/x64 Release 独立编译及真实启动；业务小样、升级/回退后运行及 Debug 仍待完成 |
| 每种模式 1,000 次 | preview.3 旧候选的软件路径 x86/x64 重建、重载各 1,000 次完成并正常退出；preview.4 新固定包四模式各三轮短回归通过，但未重跑千次门槛，资源门槛仍未通过 |
| 100 次重载资源趋势 | preview.3 候选的 x64/x86 软件路径操作通过；预热 3 次、静置 1 秒，末次 Private Bytes 分别增加 6.0/7.2 MiB，低于 16 MiB 数值线。preview.4 尚未复测，对象所有权与长期趋势仍未验收 |
| 8 小时混合负载 | 未执行 |
| 系统、DPI、多屏、IME | 已登记本机精确系统与驱动；[支持矩阵](support-matrix.md)中的 DPI、多屏、IME 等仍未验证 |
| 内核维护、安全、授权、发布责任 | preview.4 固定包已换入重编译内核，x86/x64 回环正负向各六项再次通过；实际信任根与代理、完整安全维护、授权及发布责任仍未关闭 |
| Electron 性能与体积对照 | 共享场景、宿主、原始样本和报告待实现 |
| 接入、兼容和维护成本 | 待通过实际小样与升级记录采集 |
| 最终交付与三个结论 | SDK、模板、Demo、测试、benchmark、支持表、迁移/诊断手册和工程/生产/价值判定均须逐项验收 |

本文件记录执行进度，不能代替机器报告或修改 `releaseReady`。新增阶段证据应链接实际 run-id、源码与载荷身份，并列出未执行项。

当前开发版本为 `0.3.0-preview.4`。根 `verification/acceptance.json` 仍是 preview.2 基础切片的历史结果；本轮新记录在 `verification/production-validation/`，不能互相替代。模板和 Demo 默认/React 的 x86/x64 Release 回归均已通过。x86 首次完整构建有一次资源目录替换 `EPERM`；旧目录恢复后，未改源码重试通过，失败日志保留且根因未关闭。

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

随后按 [ADR-0007](../adr/0007-rebuilt-miniblink-preview-binary.md) 将相同散列的双架构候选 DLL 放入 `0.3.0-preview.4` 固定包。[新包回归](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)绑定 x86/x64 完整 Release 构建、SDK/Web/原生契约、模板与 Workbench 三入口真实 smoke、独立公共消费者四模式各三轮，以及固定 DLL 的回环 HTTPS 各六项测试，均通过。新包[仓库外项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)通过包/锁校验、双架构 Release 编译和真实启动关闭。旧 preview.3 千次与 100 次资源结果仅作历史比较；preview.4 尚未重跑长循环、资源和 8 小时门槛，`releaseReady` 保持 false。

[preview.4 完整源码包搬移实验](../../verification/production-validation/preview4-source-2026-09-23/README.md)在无 Git 的仓库外解压目录通过逐文件校验、x64/x86 Release 完整应用编译与三个入口启动关闭；这是当次源码快照的证据。新增文档和证据后需刷新最终清单与 ZIP，底层 Miniblink/SOUI 在搬移目录的源码重编译仍待执行。
