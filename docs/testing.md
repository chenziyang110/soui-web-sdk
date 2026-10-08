# 测试与验收

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

`tools/Build.ps1` 在开始构建时使旧成功记录失效；结束时检查编译输入未改变并记录载荷。`-SkipWeb` 只接受源码及 Web 输出均与上次构建匹配的记录，首次构建或文件改变后必须完整构建。通过 CMake 直接编译可以调试，但不能代替这些打包所需证据。改变页面、DLL、编译输入或目标集合后，测试和打包都会要求重新构建。

`node --test tests/sdk/delivery-identity.test.cjs` 验证源码换行、文件集合、路径安全、资源篡改、部署清理、安装字节身份和旧证据失效。

外部 CMake 接入使用 `tests/package-consumer/`：先安装 SDK，再配置 CMAKE_PREFIX_PATH 指向实际的 `out/install/x64`，以相同工具集构建并运行。SDK 单独构建和外部链接分别验证。

本次交付结果见 `verification/acceptance.json`。未执行项不能用历史结果补齐。生产验收另需目标 Windows 实机、DPI/多屏/输入法、持续运行、重复关闭、重载内存、安装升级与回退、故障恢复验证。

完成四组构建、依赖重编译与运行、外部 CMake 和独立项目验证后，维护者可以运行 `node tools/Collect-Verification.cjs` 汇总当前交付结果。该入口会重跑单元测试，核对运行产物散列，并在证据缺失时失败；它不会把未执行项自动标为通过。

0.3 基础切片可使用 `node tools/Collect-Verification.cjs --foundation`：要求 x86/x64 Release 的三种启动场景及四个单应用运行包；重跑单元与原生契约测试。结果明确为 `scope: foundation`、`sourceDeliveryVerified: false`，不等同于完整候选验收。汇总失败会留下未验收状态，不能继续引用历史绿色结果。汇总会更新证据文件，需随后刷新完整源码清单。
