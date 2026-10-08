# ADR-0030：基准进程的精确 FILETIME 身份

## 正式运行器接入决定（2026-10-08，已实现；真实桌面待验证）

新运行记录使用 `soui.benchmark-single-run/v2`。三个角色的 `ownedEvidence` 保存本轮固定相对文件名及 bytes/SHA256：native report、launch context、原始 stdout/stderr。context 在 launch 返回后、等待 ready 前锁定散列；最终不得改变。`captureRoot` 仅用于搬移后核对原参数，不用于读取外部文件。所有读入的证据、页面文件和轨迹必须是本 run 内无链接的普通文件；截断、重复身份、路径逃逸、跨报告、散列、参数或 PID/FILETIME 不一致都拒绝。

宿主身份从本轮原生 start 派生并与真实 ready 核对，传给两个 collector 的第五参数。结束后重新读取完整 native report、验证 context 与终态并派生 subject，给分析器第四参数；正式 v2 pair 要求 `exactFileTimeBound=true`。普通 run.json/TS 的身份声明不能代替原生记录。文件散列用于绑定本轮一致性，不声称防止同用户恶意重写整组文件，也不认证进程内存。

默认正式运行要求三个 Job 自然 completed。有意 STOP 与失败清理保留原生 `passed=false`、停止原因和受控闭合结果；`controlledStopClosed` 是独立诊断，不变成自然完成，不继承编译器尾进程例外，也不自动成为 pair 准入。完整输入/采样和页面完成仍须各自验证。

旧 v1 raw 不改。读取旧 verdict 只核对原有字段投影及原 schema，不对新旧对象做整体字符串比较；返回新的明确 unbound 标记。旧预检可供历史诊断，新正式 v2 pair 不与旧格式混合准入。旧 sampler 回归改为监督 subject 与 collector，并持有实际身份和专用 STOP 控制，禁止按历史 PID 清理。

- 日期：2026-10-08
- 状态：implemented；独立 x86/x64 原生协议和正式 reader 的本机无窗口验证完成，真实桌面与性能验收待完成
- 范围：外部输入/进程树 collector、轨迹完整性分析器及正式运行/读取入口；不修改 SDK、运行时或性能阈值

## 背景

主线整合后的测量工具准备指纹同时包含 `owned-tail-evidence.cpp`、`owned-tail-evidence.h` 与 `cmake/SouiWindowsSdk.cmake`。任一实际编译输入缺失或变更都会在启动标准测量组前拒绝旧准备记录；两架构的纯夹具回归只证明这一拒绝边界，合成 PE 不构成真实工具链或源码编译证明。实际工具重编译、输出 COFF/SHA、原生 Job 与标准组结果另行保留。编译退出 0、通用监督失败以及受控尾进程闭合仍是分别记录的结论；测量准入不继承编译器尾进程例外。

现有两个 collector 的 v1 记录将进程创建 FILETIME 与开始时 UTC FILETIME 输出为 JSON Number。当前 FILETIME 大于 JavaScript 的最大安全整数；两个实际相差少量 100ns tick 的创建身份可能在 JSON.parse 后相等。现有分析器明示只在 JSON Number 精度下比较，但报告没有机器可读字段阻止该结果被当作原生精确身份绑定。

测量工具需要区分精确的原始值存储、同一实际进程的绑定和测量精度。保留 FILETIME 的全部位数不提高 GetSystemTimeAsFileTime 的时间分辨率，也不把 UTC 变成单调时间。进程 PID 可以复用，不能单凭 PID 或时间容差识别本轮宿主，更不能据此授权终止其他任务。

## 决定

1. 生产输出升级为 `soui.process-tree-samples/v2` 与 `soui.external-input-trace/v2`。创建与 UTC FILETIME 字段名不变，但类型为规范十进制字符串：正数、无前导零、符号、小数、指数或空白，且不超过 UINT64_MAX（18446744073709551615）。所有逐进程 creationTime100ns 同样为字符串。PID 仍为 1 至 UINT32_MAX 的安全整数。
2. 两个原生工具使用同一个私有 serializer，输入为原生 uint64_t，不经浮点转换。GetProcessTimes 返回值与 FILETIME 高低部分完整保存。既有 Win7 可用 API 保持不变，不提高 Windows 最低版本。
3. 分析器继续读取 v1/v1 旧记录，明确返回 `exactFileTimePair=false`、`exactFileTimeBound=false` 和旧数值时间对齐范围。不得从已舍入的 Number 构造字符串或 BigInt 再宣称恢复原值。混合 v1/v2 对拒绝；旧原始文件不重写。
4. v2/v2 对使用边界受限的 BigInt 解码，严格比较 host PID/创建字符串及每个 sample 的 root 身份。新记录不能用 Number 代替字符串以静默降级。未知版本、非法 PID、同 sample 重复 PID、root 创建身份变化和无效成员创建值拒绝。
5. 跨 collector UTC 差值、负值与时段覆盖判断均使用 BigInt，在做差前不转换为 Number。保留 `inputOffset100ns` 作为精确的十进制差值；派生 inputOffsetMs 只在差值可安全转换时用于展示。区间判断不依赖该派生浮点值。
6. 两个 collector 在原有四个参数后接受可选第五参数 EXPECTED_CREATION_FILETIME。打开并保留目标进程句柄，读取真实创建值，然后在任何输出文件创建、窗口定位、窗口移动或 SendInput 前严格核对。缺少 expected 的 v2 header 明确记录 expectedCreationTime100ns=null。实际输入前再次核对持有进程仍存活、窗口归属和前台；进程退出不得继续。
7. `checkRunTraces(input, process, scene, {ownedSubject})` 接收真实监督器返回的 `{pid, creationFileTime}`；只有新对全部身份一致、两个 collector 事先绑定 expected，且与该持有的 subject 精确匹配时，`exactFileTimeBound=true`。不提供 subject 时可做独立新轨迹完整性检查，但只报告 `exactFileTimePair=true`、`exactFileTimeBound=false`。传入 subject 的调用遇到旧轨迹、缺少 expected 或不一致身份必须失败。调用者负责核验 subject 来自本轮完整 owned Job 记录；普通可写 JSON 不是授权或持有证明。
8. 精确身份通过不代表进程树枚举无遗漏、输入已被页面消费、无外部负载、实际呈现或生产性能通过。原有 physicalPresentation/performanceGate/releaseReady 不扩大。

## 备选方案与兼容性

- 保持数字并增加误差容限：仍无法区分 PID 复用和相邻创建 tick，拒绝。
- 给旧 Number 重新加引号：无法恢复丢失位，拒绝。
- 全部大整数改成字符串：会扩大资源/QPC 协议迁移，本切片只改变 FILETIME。
- 直接拒绝所有旧记录：阻止历史诊断，改为保留可读和显式不精确标记。

原有四参数 CLI 仍可执行，但不能声称事先绑定监督器宿主。新参数顺序：ProcessTreeSampler PID INTERVAL_MS DURATION_MS ABSOLUTE_NEW_OUTPUT.ndjson [EXPECTED_CREATION_FILETIME]；InputTrace PID SCENE ABSOLUTE_NEW_OUTPUT.ndjson full|probe [EXPECTED_CREATION_FILETIME]。正式运行器必须把本轮 ready.creationFileTime 传给两个工具的第五参数，以及分析器第四参数 ownedSubject，并要求 exactFileTimeBound。新输出 schema 和 FILETIME 类型必须共同升级。历史汇总可继续使用无 subject 的调用，但必须展示未精确绑定；严格比较旧已存 verdict 的 reader 需保留旧字段投影比较，并显示新结论的 Bound=false，不能重写原始记录。

SendInput 向系统输入流发送事件，不是原子地向专属 HWND 发送。检查前台与 API 调用之间仍有不可消除的切换竞态，成功返回也不能证明页面已经消费输入，更不能称为真人手势。当前仍要求独占可交互桌面，并通过页面事件验证结果；不以本次身份修复关闭该限制。[官方 SendInput 契约](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)。

## 验证

先保留相邻 FILETIME 在旧 Number 中坍缩的受控 RED。Node 回归覆盖相邻 tick、最大 uint64、溢出、格式、PID、root 复用、混合版本、缺少/伪造 owned subject、UTC 差值和覆盖边界。私有原生 console fixture 使用与两个真实 collector 相同的 serializer 生成两种 header，并由分析器解析；fixture 是协议测试数据，不是性能采样或实际输入证据。

两个真实 collector 和该 fixture 在独立目录进行 x86/x64 最小 MSVC 构建；只有新 spawn 的进程受持有 Job 管理，完整保留编译退出与尾进程闭合结果。不启动性能 GUI、VM 或键鼠输入。真实 InputTrace 输入和真实呈现将单独预约验收，不能由 schema fixture 替代。

## 本机切片证据与限制

本节保留独立 collector 工作树完成时的事实；正式 reader 的后续整合及验证见本文末尾。不同阶段的源码身份和证据不得互相投影。

工作树基于 05e7d93b，生产 SDK、版本、清单及正式入口均未更改。独立入口 `out/exact-identity-validation/run.cjs` 保存七个源码输入、实际 runner/client identity、EXE context、原始 stdout/stderr 字节与 native NDJSON。源码输入在每轮开始及结束时逐个重验 SHA；编译选择 v143、SDK 10.0.19041.0、C++17，局部禁止 MSBuild 节点复用和 vcpkg 自动集成。

`build-960217f6-b4f4-4eae-b57f-3a4cb14bd428` 的两架构 configure/三个目标编译本体均 exit 0，无 C++ warning/error 匹配。x86 configure 留一个 vctip，监督器清理后 Job 归零，passed=false 原样保留；另外三个 Job 自然闭合。编译成功不覆盖这一次外层失败。

`native-d09c4cd4-3136-4625-ada4-af30e371c747` 的两架构 Node 回归各 13/13、无 skip，包含实际原生 serializer 的 v1/v2 两种 collector header 与 uint64 边界。实际正确 Sampler 在三个完整样本的文件条件满足后，受控 subject 才结束；采样器及 subject 均 native exit 0、Job 自然归零，逐进程创建值与 expected/host 字符串实际相符。实际 InputTrace 与 Sampler 各自传错一个 tick，均精确报创建身份不一致、exit 1，输出文件不存在，自己的 Job 已闭合。失败 Job 对 console helper 的即时清理原样保留，不能称为自然通过。

第一轮夹具 `native-d788f1fe-c395-4864-b2e1-47a00a57da93` 使用固定五秒 subject；在调度和子进程收尾耗时后 subject 已先退出，真实 Sampler 没有样本而 exit 3。失败证据没有改写，后续只改 out 夹具为显式完成文件条件，未放宽采样器要求或改生产源。RED 的旧 Number 坍缩及缺少 exact 字段保留于 out/exact-filetime-red。

这些 ignored out 记录尚待正式归档。测试没有实际 SendInput、性能 GUI、VM、真实 PID 循环复用或 PresentMon 采集，也没有证明整进程树枚举不漏短命进程。父 PID 复用的源码过滤同时要求新观察到的后代创建时间不早于实际父进程；这只是必要检查，不是完整 owned Job 成员证明。正式 run-one/check-pair 与旧 verdict 投影接入由根整合，当前尚未完成。

## M0 后续实际呈现建议（仅资料核对）

2026-10-08 查阅官方发布页显示最新列出的发布为 PresentMon v2.6.0。后续固定该 release/tag、实际二进制散列、采集参数及 CSV 列，优先研究无 overlay 的 console 与原始 ETW/ETL 路径；本次没有下载、安装或运行它。[官方发布](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0)。

必须区分应用 Present 调用、系统追踪到的 displayed、屏幕实际光学变化。官方列定义中 MsBetweenPresents 是应用提交间隔；DisplayedTime 对未显示帧为 NA，DisplayLatency 与 MsUntilDisplayed 的起点也不同。不能用提交 FPS、JS rAF 或呈现管线事件直接代替显示器上的输入到光子时延。冻结具体列及帧/丢弃语义后，再用外部传感器或高帧率摄像建立物理屏幕验证。[v2.6 console 定义](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md)。

多进程浏览器可能由 GPU 子进程而非 host PID 提交 swapchain。应先用原始进程生命周期事件与本轮 owned member PID/FILETIME、采集窗口核对，再固定 SwapChainAddress、PresentMode、显示 source/layer；不能按名称或简单汇总所有 DWM 帧。软件 GDI/离屏绘制是否能归因到应用帧必须先做真实试验；无法区分或指标缺失就记不可用，不用 JS 指标补成通过。这是接入建议，未声称已验证 SDK 各后端。

官方完整 Intel PresentMon 产品说明支持 Windows 10/11；console README 仍有 Win7 强制关闭稳定性注意事项，不能据此推断最新整套工具已验收 Win7/PE。后续逐 OS 核对 ETW provider/权限、驱动与可用指标，并设计只停止自身 trace session 的有界正常关闭。HWS 下 GPU 指标及 Runtime=Other 下部分 CPU/延迟指标也有精度限制，设备报告必须保留。[官方产品支持](https://game.intel.com/ar/intel-presentmon/)，[v2.6 平台与精度注意事项](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README.md)。

## 正式 reader 接入验证（2026-10-09）

`sm05-mi/out/measurement-integration-validation/native-d0d0adb8-e07c-402e-8fbe-e7c11fe2d893` 以 foundation `2154ed80` 的监督 client/runner 和已经验证的 collector EXE 运行，无重编译。开始/结束逐项核对 16 个输入源码与六个 EXE 的 SHA；inputs 保存 EXE COFF、Node、runner/client 的实际字节身份。两架构各 45/45 Node 回归、无 skip，外层 native Job exit 0、completed、active 0、未强制终止。每个实际 sampler 场景的内部 native report、context、日志、ready 与结果另保留在 `out/sampler-owned-*`；实际 serializer 位于 `out/native-identity-*`。

本次覆盖真实 Sampler 的完整多样本、普通及父退出后后代、超时、输出已存在不覆写；两 collector 的 wrong-one-tick 原生拒绝、无输出；serializer 的字符串边界；reader 的原始报告/context/hash、重复身份、截断、错误 FT、链接、搬移；正式 v2 pair 的 authored 夹具准入、JSON 伪造与 STOP 拒绝；legacy 投影和 series mixed/unbound 拒绝。超时/wrong-tick 内部 Job 保留 generic passed=false 和实际清理字段；没有投影成普通成功。新 v2 配对 reader 的完整准入夹具明确是 authored JSON/context 数据，不能代替真实 v2 桌面配对。

`red-reader-2ae9cfc7-8b12-46e5-97ef-0d8120062be6` 保留 Git baseline reader 原字节、SHA、harness、旧 raw 逐文件散列及失败栈。把原 reader 与本次导入的新 analyzer 组合，实际复现 `recorded trace verdict changed`；当前 reader 对同一未改历史 raw 成功返回 legacy-unbound。该 RED 是 reader/analyzer 组合回归，不是旧原生二进制或性能重放。

正式 run-one 尚未执行正确 InputTrace、可见宿主、五轮桌面、Modern 背景进程 drain 校准、物理呈现/高刷或 OS/PE 矩阵。保留原有 drain/时长；三个自然 Job 的规则不会继承 compiler-tail policy。根集成和串行真实桌面验证仍是后续门槛，`benchmarkValid`/`releaseReady` 继续为 false。
