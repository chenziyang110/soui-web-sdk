# 测试与验收

统一桌面开发包工具回归：`node --test tests/desktop/package.test.cjs`。它验证参数、
目标身份、步骤失败中断、拒绝覆盖/链接和散列，不以模拟命令冒充真实编译。
完整构建/CTest/安装/压缩入口及平台限制见 [统一桌面入口](unified-desktop.md)。

[当前发布修复记录](../verification/production-validation/0.5-release-fixes-2026-10-16/README.md)保存两架构 UIA 专项、实际读屏文本与表单操作、有限资源校准及依赖审计；最终公开构建和交付物另行验证。

可选 UIA 的 `owner-sync`、`owner-focus`、`uia-mutations` 和 `uia-focus` 覆盖窗口线程及真实 Windows UIAutomationCore 同步操作。各用例按 CTest 注册在独立进程运行；前序 UIA 客户端可能建立进程 MTA，不能把同进程 `all` 的 COM 初始化状态当作独立用例。填写与选择分别计数，Windows 可能另外请求焦点；每次业务仍须经过授权。

[TaskStore 校准消费者](../tests/calibration/README.md)通过安装后的 SDK
实际执行最大合法参数、1,024 条历史与 32 个在途任务，输出独立原生
额度峰值。它不替代真实页面、GUI、长期与目标系统验收。

`sdk.native.soui-dependency-contract` 使用实际 SOUI/Utilities 导入库和 DLL，
验证原有单例数据导入、尺寸格式化及局部工厂注册后的对象创建与释放。
它覆盖 LLVM 重建曾丢失的数据导出，不代替完整窗口、图像或目标系统测试。
组件来源与真实桌面验收使用 `Test-Dependencies.ps1` 的显式 LLVM 输入，
详见[构建文档](build.md#显式-llvm-组件重建)。

LLVM22/MSVC142 候选的汇总使用 `node tools/Collect-Verification.cjs --native-toolchain LLVM22MSVC142 --with-gui-bindings`。
先完成两架构实际测量工具准备；汇总会传递各自的固定请求，仍由现有读取器
核验真实工具、构建、进程和载荷。省略档位时保留 MSVC143 行为。
构建观察保存当时完整源码清单的不可覆盖副本，构建期间活动清单必须不变；
后续元数据更新不能替代当前源码与编译依赖校验，最终源码包另行刷新并复验。

LLVM 发布验证可显式指定 `-LegacyDevkitRoot`，让历史 MSVC 验证工具使用独立源码目录；
`-DevkitRoot` 继续选择 LLVM 工具目录。两个目录必须真实存在且物理独立，
入口会在工具构建前拒绝链接别名和相同目录。省略新参数时保持原来的 MSVC 目录选择。
已有 LLVM 工具需保持记录有效时，不传 `-FreshConfigure`，入口将核验并复用有效的准备结果。
基线与回退使用历史 MSVC，新建、升级、再次升级和搬移使用 LLVM；各阶段仍实际编译和启动。
见 [ADR 0105](adr/0105-release-validation-stock-ownership.md)。

JSON 释放回归在普通和诊断配置下，验证析构零分配、十万层嵌套与异常展开。
公共 Bridge 的逐次分配失败测试只在 SDK 调用期间注入故障；诊断读取返回 `busy`
时在五秒内重新采样，必须取得 `ready` 后验证原有任务、队列和请求字节均已回滚。
不可将忙碌快照中的清零字段视为通过。LLVM 单元入口还运行 WebView2 依赖回归，
对两架构的辅助头文件验证认证归档成员，并拒绝缺失、重复和被篡改的成员。
构建命令索引回归验证工具别名、参数大小写、重复命令与原始记录，并用调用次数
确保每条记录只规范化一次；性能修复不改变原构建校验期限或身份验证要求。
CRT 参数回归按真实 SDK 条件提取分支，兼容前置的 RuntimeDoctor 独立策略；
重复分支、错误归属和附加语句仍拒绝，原参数、重复选项和系统宏断言保持执行。
Modern 汇总只接受 v2：复验当前构建、原生 PID/FILETIME、实际 Owned 原始记录和
本轮日志来源，拒绝旧 PID 或文字成功标记。外部工具目录必须由调用者显式指定，
不能从待验记录中加载代码。标准单元入口按锁文件安装 acceptance 工具依赖，
安装失败保留独立日志并返回失败。历史构建刷新夹具覆盖 x64 与 Win32/x86 映射。

0.4 增加运行档位、帧计时和 WebView2 异步准备测试。`Build.ps1 -WithWebView2` 构建可选组件和现代模板，之后执行 `Test.ps1` 与 `Test-Modern.ps1`；后者绑定当前构建及运行载荷。`experiments/webview2-consumer` 现为安装后公共现代组件的独立消费者，仍保留历史目录名。现代项目支持在仓库外运行自己的 `tools/Test-Modern.ps1`。详见 [现代后端](modern-backend.md) 和 [0.4 门槛](roadmap/0.4.0.md)。

preview.10 增加 `sdk.native.action-contract`、Web动作契约与GUI语义测试，标准 `Test.ps1` 纳入这些检查。模板实际就绪探针包含契约发现→工具定义→语义动作→C++结果→状态等待→原生非法参数拒绝；WebView2实验执行相同业务与受限能力拒绝。`tools/Verify-Release.ps1` 在冻结版本后执行仓库外生成、升级/回退、搬移和真实启动，不复用历史结果。

2026-10-03 起，独立消费者监督器使用单调计时判断步骤和无进展超时；系统 UTC 只用于日志。`tests/acceptance/monotonic-supervisor.test.cjs` 覆盖系统时钟前后跳变。此变更不说明历史挂载超时根因已查明。WebView2 实验使用独立 [构建与测试入口](../experiments/webview2-consumer/README.md)，不加入默认 SDK 构建；它的记录不能替代 Miniblink 或完整产品支持矩阵。双架构实验与 Miniblink 回归的 [2026-10-03结果](acceptance/webview2-experiment.md) 保留失败证据和未覆盖门槛。

preview.9 新增 `sdk.native.host-adapter`（引擎无关转发、失败重试、终态与晚到事件）、扩展 `sdk.native.contract`（方法发现与授权/分页/响应边界），以及 `node --test tests/web/capabilities.test.mjs`（Web 分页与结果校验）。`tools/Test.ps1` 已纳入这些检查。模板在 `bridge.probe.report` 之前执行 `discover()`，因此本版模板的真实启动包含当前原生发现往返。当前执行结果见 [本轮记录](acceptance/agent-foundation.md)，这些测试不代表完整 Agent 操作或第二后端支持。

`demo.native.interaction-task` 通过真实 Workbench adapter 和公开 JSON Bridge 验证有界 `demo.interaction`：单调时钟计时、后台进度事件、运行中取消、会话隔离、非法类型/整数范围与溢出、关闭会话清理，以及原快速任务兼容。构建后可运行 `ctest --test-dir out/x64 -C Release -R demo.native.interaction-task --output-on-failure`，x86 使用对应目录。这是无窗口的原生集成回归；V02 仍需新构建上真实滚动、IME、缩放和公共任务并发的桌面证据，不能继承旧应用载荷的短测结果。

Workbench 普通窗口初始位置使用主显示器的工作区：空间充足时居中；只要仍可放下至少 640×480 的窗口，就给右下缩放方向预留至少 84×64 像素；再小的工作区退回到可见范围内。任务栏占用的区域不计入工作区。`demo.native.window-placement` 覆盖 Win7 1024×768 与 800×600 底部任务栏、默认大窗口与显式 850×640 窗口的 80×60 缩放目标、负坐标显示器及无效区域。纯几何测试不能证明真实边框命中和拖拽；[现代 Windows x86/x64 原生缩放记录](../verification/production-validation/work-area-placement-gui-2026-09-28/README.md)已证明本机 1920×1032 工作区中的两架构 80×60 鼠标拖拽与正常关闭，但不证明 Win7 SP1 x86、页面在 708×488 下可用或完整 V02。须用新冻结输入在 Win7 记录真实窗口矩形、任务栏区域、原生缩放输入与关闭结果，再判断 [Issue #42](https://github.com/chenziyang110/soui-web-sdk/issues/42) 是否关闭。

`node --test tests/sdk/sdk-native-payload.test.cjs` 验证 SDK Core 排除已知历史内核备份，保留有效 DLL/LIB 和许可材料，并执行旧包安装、新包升级、旧包回退。原始源码备份和业务文件必须保持原字节；`Test.ps1` 和交付汇总均包含此回归。

`kernel.native.scheduler-fairness` 编译内核实际调用的内部选择策略，覆盖新任务优先级、同级先后、长期等待、连续普通任务、未来任务资格和退出清理。它是策略回归；修改此策略仍必须在两架构完整重编底层依赖，运行保持原动画的独立消费者及真实应用，不能仅凭该测试放行 DLL。首次红绿结果见[原始记录](../verification/production-validation/scheduler-fairness-policy-2026-09-26/README.md)。

preview.13 的 `node --test tests/sdk/installation-interruption.test.cjs` 真正终止独立安装子进程，再由新进程恢复。覆盖准备、journal、替换、删除、新增、锁提交、清理、恢复再次中断，以及并发、文件/备份/锁/journal 冲突和链接。原始边界与退出记录保存在每次独立的 `out/sdk-recovery/attempt-*/`，故障样本不删除。`Test.ps1` 和交付汇总入口均执行此回归；小文件事务通过不能代替真实 SDK 包、业务编译、旧无 journal 现场或目标平台验收。

preview.11 增加 `sdk.native.session-renewal` 与安装后独立 [session-consumer](../tests/session-consumer/README.md)，验证授权期限、轮换、重取、撤销、稳定归属与公开接入。Web 续期测试使用模拟时间并覆盖连接器与真实注入 resolver 的容量；这些测试不启动窗口，不证明真实八小时、模态响应、休眠或 Win7。真实长测必须在同一版本载荷上跨越多次 25 分钟续期，保留任务、订阅、输入响应和资源轨迹，不能用反复重载绕过授权期限。

preview.10 新增 `sdk.native.menu_service` 和 `sdk.native.global_shortcut`，通过未安装的平台替换边界验证参数、状态、故障及资源所有权，不调用真实菜单或注册快捷键。安装后独立消费者同时使用三项公共服务；`--smoke-test` 的菜单程序取消与快捷键冲突也是实际桌面负载，必须串行。Workbench `--resident --resident-smoke-test` 是三窗口关闭、重载、重建和无窗口驻留的有限短测，不能证明真实快捷键、任务隔离、Explorer 重启或长期稳定。实际证据需绑定当前源码与完整载荷，历史版本结果不自动继承。

preview.9 的 `sdk.native.tray-service` 使用未安装的私有平台边界测试托盘故障和资源状态，不创建窗口或调用真实 Shell。独立 [tray-consumer](../tests/tray-consumer/README.md) 必须只从安装后的 SDK 构建，其 `--smoke-test` 会使用真实桌面，须与其他桌面/QEMU 负载串行。编译、模拟故障、Shell API 返回成功均不能替代图标可见、真实点击、Explorer 重启和目标平台验证。

0.3 后续的场景、公共 SDK 消费者、Electron 对照及生产准入设计见 [SDK 生产能力与价值验证方案](roadmap/0.3-sdk-validation.md)。其中独立消费者与生命周期驱动已落地，混合负载、完整 Electron 对照及生产准入档尚未实现；下文是当前可用入口。

独立公共 SDK 消费者现有入口为 `powershell.exe -File tools/Test-Acceptance.ps1 -Architecture x64 -Scenario all -Cycles 3`。支持 reload/recreate/multiwindow 的真实 Host 生命周期与 Runtime 退出边界，以及 `bridge` 的标准连接器/32 并发/延时/错误恢复探针；说明见 [测试应用](../tests/acceptance-app/README.md)。`-PrepareOnly` 只生成已绑定身份的消费者，不执行任何桌面场景。该入口不包含方案中的全部场景，不授予生产或长稳通过结论。

Web 边界测试使用 `node --test tests/web/sdk-client.test.mjs tests/web/native-resolvers.test.mjs`；后者读取 Host 实际注入的 resolver 脚本验证超时、退出和计时器释放。`tests/native/miniblink_header_abi.test.cjs` 逐符号比对交付动态绑定头与实际内核导出声明，防止 x86 的 `bool`/`BOOL` 返回 ABI 回归。`ctest` 增加原生请求预算的容量、并发 ID 与跨重载占用测试。这些测试不能代替真实 DLL/页面集成；通过单元层不能声称原生任务服务或事件模块已交付。

桥接故障定位可在单次隔离运行前设置 `SOUI_BRIDGE_TRACE=1`；Host 只把阶段、请求 ID、线程和响应字节数写入系统临时目录的 `soui-bridge-trace-<pid>.log`，不写请求或响应正文。该诊断默认关闭，日志不能替代监督器结果。

完成构建后执行：

```powershell
powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release
```

覆盖 SDK 包校验、冲突与回滚；Web SDK 请求关联、错误和超时；原生桥接注册、会话、令牌与来源校验；模板和 Workbench 的实际启动、页面就绪、桥接调用及关闭。应用 smoke 使用生产默认的软件呈现路径；GPU compositor 需独立运行、保留退出码与故障证据，当前不能由默认 smoke 代替。`-NativeOnly` 仅执行桌面启动验证。

GPU 内核候选应通过 `Test-Acceptance.ps1 -RuntimeBinary <候选 DLL> -Scenario recreate` 在隔离消费者中验证，分别设置 `SOUI_GPU_COMPOSITOR=1` 与需要的 `SOUI_GPU_DIRECT_PRESENT` 值，并保留 `build.json`、`result.json`、逐轮 `events.ndjson`。设置 `SOUI_GPU_SHUTDOWN_TRACE` 为绝对文件路径可记录 WGL 上下文及 DC 释放返回值；诊断默认关闭。当前结果与软件路径对照见 [GPU 专项记录](acceptance/gpu-lifecycle.md)。`tests/diagnostics/wgl-context-lifecycle.cpp` 是隔离系统 WGL 行为的手动探针，不代替 SDK/Skia 整体路径。

HTTPS 内核的[回环证书验证](../verification/production-validation/preview4-fixed-2026-09-23/README.md)使用 `tests/diagnostics/tls-loader.cpp`、`https_server.py` 和 `tools/Test-TlsKernel.ps1`：证书及私钥仅生成于忽略目录，分别检验未受信链、受信主机、错误主机、回调试图关闭两种校验及重定向。preview.4 固定 DLL 已在双架构重跑；该专项不代表旧 TLS 依赖已有持续安全维护，也不验证实际信任根及代理。

桌面测试需要可交互 Windows 会话，脚本串行运行并使用互斥锁。结果在 `out/<架构>/smoke-<配置>.json`，绑定构建 ID、源码身份及全部 EXE/DLL/HTML/JS/CSS 资源。模板、Demo 默认入口、Demo React 入口分别验证。单元测试不能替代真实宿主启动。

拟议 Owned 桌面迁移保持上述 `soui.native-smoke/v2` 字段和 ready/close 行为。`Test.ps1` 的桌面段持有原有单一租约，调用共用 `desktop-smoke.cjs`；每个应用使用所选架构已准备的 OwnedProcessRunner，要求真实自然 0/0、零存活成员及无强制清理。每次新建 `out/<架构>/smoke-owned-<uuid>/`，保留前一次 smoke JSON、应用旧日志、当前 raw/context/finished/stdout/stderr 与新日志；失败不覆盖旧原始样本。新报告的 Owned 扩展另核对实际参数、执行文件和 runner 散列，旧 v2 记录保持历史含义。

统一 Soui 的 desktop scope 在已有外层租约内派发 UnitOnly，再直接运行共用 helper，避免嵌套调用完整 Test.ps1 造成第二租约冲突。controller 的 Owned 闭合与各应用的 Job 闭合分开核验。UnitOnly 会尝试准备验证工具，因此统一入口在它前后复核正在使用的 runner 字节；锁定文件、重建或替换失败均保留实际错误。独立 Test.ps1 的旧前置单元命令仍未全部迁入 Owned，不能声称整个独立脚本已完成监督迁移。新 resource/session/devkit reader 测试必须实际存在，不跳过缺失文件；session、52 分钟、8 小时及资源预算运行仍是各自显式入口。本候选尚未执行，05-09a、长期稳定性和发布资格继续待验收。

`tools/Build.ps1` 在开始构建时使旧成功记录失效；结束时检查编译输入未改变并记录载荷。`-SkipWeb` 只接受源码及 Web 输出均与上次构建匹配的记录，首次构建或文件改变后必须完整构建。通过 CMake 直接编译可以调试，但不能代替这些打包所需证据。改变页面、DLL、编译输入或目标集合后，测试和打包都会要求重新构建。

`node --test tests/sdk/delivery-identity.test.cjs` 验证源码换行、文件集合、路径安全、资源篡改、部署清理、安装字节身份和旧证据失效。

外部 CMake 接入使用 `tests/package-consumer/`：先安装 SDK，再配置 CMAKE_PREFIX_PATH 指向实际的 `out/install/x64`，以相同工具集构建并运行。SDK 单独构建和外部链接分别验证。

完整验收状态见 `verification/acceptance.json`；0.5.0 的限定范围结果见[发布说明](releases/0.5.0.md)。根文件的 `collectionExecuted=false` 表示未运行完整收集，引用记录不授予验收通过。未执行项不能用历史结果补齐。生产验收另需目标 Windows 实机、DPI/多屏/输入法、持续运行、重复关闭、重载内存、安装升级与回退、故障恢复验证。

限定范围交付后，按[发布流程](releasing.md)使用 `--record-unverified --evidence-reference <相对路径>` 记录当前版本的未验收状态并保留旧报告，不为更新元数据重跑已推迟的矩阵。`node --test tests/sdk/verification-status.test.cjs tests/sdk/verification-profile.test.cjs` 检查该入口及原收集档位边界，不代表原生或生产验收。

完成四组构建、依赖重编译与运行、外部 CMake 和独立项目验证后，维护者可以运行 `node tools/Collect-Verification.cjs` 汇总当前交付结果。该入口会重跑单元测试，核对运行产物散列，并在证据缺失时失败；它不会把未执行项自动标为通过。

0.3 基础切片可使用 `node tools/Collect-Verification.cjs --foundation`：要求 x86/x64 Release 的三种启动场景及四个单应用运行包；重跑单元与原生契约测试。结果明确为 `scope: foundation`、`sourceDeliveryVerified: false`，不等同于完整候选验收。汇总失败会留下未验收状态，不能继续引用历史绿色结果。汇总会更新证据文件，需随后刷新完整源码清单。

## 命名缓存数据与真实接口

`node --test tests/acceptance/native-cache-validation.test.cjs tests/sdk/long-run-cache-observation.test.cjs tests/sdk/long-run-policy.test.cjs` 只验证 DTO、采样范围和监督契约；C++ `soui_cache_observation_tests` 只验证消费者数据逻辑与 ABI 布局。独立真实 Host 用例 `sdk.native.engine-host-capacity.renderer-cache-abi` 还需绑定当前源码重编的内核、对应 SHA-256 和独立资源，按原 30 秒限制执行。它检查接口参数、初始化/线程边界、异步完成副本及关闭后的晚任务，不能代替真实缓存资源或目标系统验收。

公开八小时 mixed/session 入口按 [ADR-0071](adr/0071-long-run-cache-sampling.md) 明确传入 `--cache-observation=1`。原生采样时间、重复和退休副本保留在报告里；命名缓存数据本身不会把资源、缓存限额或八小时门标为通过。

## 明确请求厂商回归

`node --test tests/sdk/vendor-tests.test.cjs` 调用真实 CMake 检查默认 OFF/ON、缺根显式 ON、不完整根、目录替代源码、SDK tests OFF/vendor ON 和缓存 ON 后输入缺失。临时配置工程不编译私有内核，完整输入形状使用合成普通文件，因此便携源码也能运行这些策略回归。[2026-10-16 专项记录](../verification/production-validation/issue-maintenance-2026-10-16/README.md)另保留真正的两架构默认 ALL 构建及准确 54 项既有执行证据。

`powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release -RequireVendorTests -UnitOnly` 要求当前根构建的私有厂商测试为 ON；x86 使用对应架构。`-UnitOnly` 仅省略桌面 smoke，仍执行原生 CTest。`-RequireVendorTests` 不能与仅做桌面 smoke 的 `-NativeOnly` 共用。

所有单元测试模式先读取实际 `out/<架构>/CMakeCache.txt`，核对源码根、构建目录、架构、配置和 SDK 测试开关，再调用实际 CTest 的 `--show-only=json-v1`。ON 必须注册 [清单](../tools/internal/vendor-test-names.json) 中完整的 54 个名称，不能缺少、重复、额外注册或禁用厂商用例；随后仍运行私有 ABI 比较及原有 CTest，注册成功本身没有通过资格。即使未指定 `-RequireVendorTests`，配置为 ON 时也采用同一完整注册要求。OFF 会明确报告厂商回归及私有 ABI 源码比较未验收；[公开 Host 断言](../tests/native/miniblink_public_host.test.cjs) 始终随单元测试执行。缺少配置、空注册或查询失败均不能视为成功。
# Installed LLVM consumer short process capture

Installed acceptance and release builds use the SDK's freshly prepared Host64
observer with a separate `soui.host64-build-debug-inputs/3` input. The only inner
entry is pinned CMake with the fixed consumer directory and seven build arguments.
Ordinary SDK builds retain input version 2 and their fixed PowerShell entry.
Original Owned process streams and native debug streams remain separate. The
consumer reader revalidates both streams, identities and frozen input pins, then
checks the actual compile/resource/archive/link evidence. A configured manifest
tool is distinct from an executed process: the exact recognized CMake 4.2.3
nonincremental consumer path embeds the manifest directly through the linker.
Other paths retain the conservative manifest process requirement. No polling or
native process rows are fabricated. See ADR 0099 for the fixed conditions.

Parser regressions: `node --test tests/sdk/host64-build-debug-observer.test.cjs
tests/acceptance/llvm-installed-entry.test.cjs`. These controlled parser fixtures
are not native build or release qualification; the real SDK and installed consumer
must be rebuilt with the current observer before resource and release acceptance.

依赖材料部署回归：node --test tests/sdk/legal-notice-materials.test.cjs。校验全部声明及许可原始散列、实际部署字节和纯文本附件路径；真实宿主资源拒绝边界另见 ADR 0107。

长期额度读取回归已接入 Test.ps1 与 Collect-Verification：`tests/acceptance/long-run-task-quota.test.cjs`、`tests/sdk/long-run-policy.test.cjs`、`tests/sdk/long-run-cache-observation.test.cjs`、`tests/acceptance/resource-validation.test.cjs`。这些模型检查不能替代公开入口中的真实长测；新额度子门见 ADR 0108。

UIA 通知验证见 [状态通知](acceptance/uia-state-notifications.md)；可选构建、实际安装和关闭组件拒绝见 [UIA 交付](acceptance/uia-build-delivery.md)。最终四配置构建须显式选择 `-WithUia`，CTest 才注册该模块的 19 个独立进程用例。

UIA 原生断言失败会保留 HRESULT。`mutations` 焦点失败还记录客户端返回时及 owner 完成后的 QPC/线程/焦点快照；正常成功路径不增加控制台输出。客户端到 owner 回调的间隔包含请求处理和 owner 等待，不能视为精确队列驻留时间。原 250 毫秒等待、实际 HWND 焦点与业务断言保持不变；未复现的历史失败仍需保留。

`node --test tests/sdk/release-stock-routing.test.cjs` 直接执行发布入口中的安装 SDK 函数，以命令记录器核对 x64/x86 的观察器准备、六项工具输入、实际回执散列和失败即停。它不启动编译器或应用，不能替代公开 `Verify-Release.ps1` 的实际消费者验证。

共用 CMake 配置日志按 [ADR-0111](adr/0111-immutable-cmake-configure-log.md) 留存每次原始副本。`node --test tests/sdk/llvm-evidence-readers.test.cjs tests/acceptance/llvm-installed-entry.test.cjs` 覆盖 SDK、安装验收和发布消费者；读取器测试不替代真实编译。

地址空间采样仅随验收消费者的 `--cache-observation=1` 启用。CTest 的 `sdk.native.address-space-contracts` 与 `sdk.native.address-space-actual` 分别覆盖故障/上限及真实私有、映射和映像区域；`tools/Test.ps1 -UnitOnly` 同时执行 `tests/acceptance/process-address-census.test.cjs`。完整资源读取结果附加 `addressSpaceObservation`，它不改变原有资源、缓存或发布判定。见 [ADR-0112](adr/0112-bounded-process-address-census.md)。

本轮原始记录：[UIA 与构建日志](../verification/production-validation/0.5-uia-and-build-log-2026-10-16/README.md)、[有界地址空间采样](../verification/production-validation/0.5-address-space-census-2026-10-16/README.md)。

UIA 成功按钮调用的真实事件订阅与拒绝/失败/关闭回归、时钟整数边界回归的原始记录见 [补充记录](../verification/production-validation/0.5-uia-invoke-and-clock-2026-10-16/README.md)。Invoked 通知不重放已执行业务，发送失败由 pump 返回；仅状态变化通知可在后续 pump 重试。

LLVM 增量构建会单独认证生成命令中的工具路径（含 Windows 8.3 路径），即使本轮没有再次运行该工具。此认证只证明工具文件身份；复用产物仍须匹配历史自然构建、当前对象及依赖，不能据此添加实际进程记录。

## 跨平台 Runtime 补充验证

开发中的 portable 档位使用 `cmake --preset portable`、
`cmake --build --preset portable`、`ctest --preset portable`。
另验证 `tests/portable/consumer` 对已安装 `SouiRuntime` 包的独立消费。
完整命令和目标矩阵见 [跨平台说明](cross-platform.md)。合成架构/拒绝测试、
本机编译、目标机运行与真实桌面验收必须分开报告；不新增远程 CI。

## 统一系统 WebView 桌面补充验证

`cmake --preset desktop`、`cmake --build --preset desktop`、
`ctest --preset desktop` 构建公共 Desktop 及本机后端。默认只运行无窗口回归：
共用 application/protocol 使用 fake backend，preload 测试提取实际生产脚本并在
Node VM 中执行。它们覆盖令牌轮换、旧消息、导航/关闭中的回调、模态重入、有界
队列、UTF-8 请求字节和 JSON 返回语义，不能替代真实引擎验收。

Linux 的 `soui_desktop_linux_integration` 始终随桌面测试配置编译；只有显式
`-DSOUI_DESKTOP_GUI_TESTS=ON` 才注册真实 GUI 用例。该用例要求正常图形会话，
无显示服务应失败，不能自动 skip 或禁用 WebKit sandbox。目标机命令、系统能力
检查及未执行项见 [统一桌面说明](unified-desktop.md) 和
[本轮实际记录](acceptance/unified-desktop-2026-10-20.md)。

