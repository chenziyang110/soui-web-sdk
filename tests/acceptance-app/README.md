# 公共 SDK 独立原生测试应用

本工程通过已安装 SDK 的 `find_package(SouiSdk)` 构建，只使用公开头文件和 API。原生 Win32 父窗口由应用提供，WebView、引擎线程、Bridge、会话与关闭行为由 SDK 提供。它不依赖模板、Demo 或 SDK 私有实现。

从交付根目录运行：

```powershell
powershell.exe -File tools/Test-Acceptance.ps1 -Architecture x64 -Scenario all -Cycles 3
```

该入口编译 SDK、安装到新目录、独立编译本工程、部署本次载荷并串行运行四个场景：

- `reload`：同一 WebView 多次重载，每轮重新完成原生连接和窗口身份检查。
- `recreate`：同一进程销毁窗口/Host，再创建新的窗口/Host。
- `multiwindow`：三个窗口同时存在，关闭中间窗口后重载另两个窗口，再创建中间窗口并退出。
- `bridge`：公共连接器、并发关联、原生等待、容量上限、公共长任务、进度事件、协作取消和错误恢复。
- `mixed`：单进程固定顺序循环 Bridge 与短业务任务、重载、最小化/恢复、三窗口及空闲，用于长时生命周期诊断；不代替外部输入。

每个新文档都会用公共 `discover({pageSize:2})` 完成多页能力发现，逐项核对该窗口实际注册的方法和 capability，并重复读取首页确认会话内稳定。重载、重建和多窗口场景因此都会重新覆盖授权后的发现路径；Bridge 场景还在任务、限流和错误恢复后复核同一目录。未安装的桌面服务不能出现在发现结果中。只有这些检查完成，页面才回报 ready。

`-Cycles` 是被测轮数，`-Warmup` 是额外预热轮数；每轮按照 `-SettleMs` 等待后采样。`-SkipSdkBuild` 只接受源码和 SDK 载荷仍与最近 SDK-only 构建记录一致的情况。测试编译输出和每次运行的原始记录保存在新建的 `out/acceptance/<suite-id>/`，失败样本不覆盖。

混合模式改用单调时长及空闲间隔，不接受 `-Cycles`/`-Warmup`。先用 `-Scenario mixed -DurationMs 60000 -IdleMs 10000` 短测；8 小时请求使用 `-DurationMs 28800000`。监督器要求总时长、每轮步骤和空闲区间完整，始终输出 `longRunGate: not-evaluated`，直到另行审查分时资源与响应指标。完成后可用 `node tests/acceptance/mixed-analysis.cjs <result.json> <analysis.json>` 重新核对原始事件散列并按小时汇总资源和操作耗时；分析报告本身仍不放行。长测应使用隔离且指纹固定的源码/SDK/消费者/内核/运行载荷，避免开发树变化导致结果身份漂移。

每个 suite 的 `consumer-source/` 保存实际编译的独立消费者源码，`kernel-input/node.dll` 保存本次测试的内核输入。v3 还复制同次构建的公共 Web SDK 到 `web-sdk/`，先使用固定 TypeScript 版本对该 SDK 的声明文件执行严格类型检查，再用固定 Vite/legacy 工具链生成 `web-output/`；`web-build.json` 记录类型检查版本、配置和日志散列，运行前复核。C++ 仍只引用安装的公共 SDK。构建和运行前后核对消费者、Web SDK、Web 输出、原生安装与最终载荷。默认使用固定 prebuilt；测试新编译内核时可显式指定 `-RuntimeBinary out/dependencies/x64/miniblink/bin/miniblink.dll`。候选只复制到新 suite 并记录为 `explicit-candidate`，不会替换固定依赖。旧 v1/v2 记录继续保留为历史证据，新的运行需要重新构建 v3 suite。

`-Scenario bridge` 通过真实公共连接器验证重复连接复用、32 个请求身份关联、200 ms 原生延时期间页面计时器继续推进、业务失败、超大响应与随后正常调用。客户端容量特意高于 Host，通过 257 个普通请求检查原生 256 上限；普通槽位占满时对不存在任务的取消应进入公共服务并返回 `E_TASK_NOT_FOUND`。随后由安装的 SDK 执行真实后台计数任务，页面确认进度事件、查询结果、订阅注销，以及运行中请求取消后的终态。`all` 包含此探针和三个生命周期模式。`-PrepareOnly` 只编译并记录身份；退出码 0 表示准备成功，不表示执行场景通过。

`node --test tests/acceptance/runner.test.cjs` 验证结果核验器拒绝崩溃、超时、缺失或重复步骤、错误窗口、参数不符和缺失采样。采样必须出现在对应步骤完成之后，提前记录 closed 样本不能通过。核验器通过本身不证明真实 SDK 场景通过。

监督器一旦判定超时，会先写入 `timeout-observation.json`：超时原因、最后已解析事件、已读字节数、当时日志长度及末尾内容。需要在终止前抓取线程现场时，可选地设置 `SOUI_ACCEPTANCE_DEBUGGER` 为与被测进程同架构的 `cdb.exe` 绝对路径，并设置 `SOUI_ACCEPTANCE_DUMP_ROOT` 为有足够空间的仓库外绝对目录。监督器以本地符号路径附加，最多等待 20 秒，在该目录的 run-id 子目录保存完整 dump 和线程栈日志；结果中记录状态、长度及 SHA-256。未配置或抓取失败仍按原超时判失败并终止进程，不能将 dump 视作测试通过。dump 可能包含进程内数据，不进入公开源码包；本验收夹具仅用确定性测试数据。`node --test tests/acceptance/timeout-diagnostics.test.cjs` 默认测日志偏移；设置 `SOUI_TEST_CDB_X86` 和 `SOUI_TEST_DUMP_ROOT` 后会另用临时 x86 进程验证真实抓取链路。

步骤和无进展期限使用 `performance.now()` 单调时钟，避免系统校时前跳造成假超时或后跳延后超时。原有步骤 2 秒、无进展 5 秒观察余量保持不变；整体进程期限仍由相对计时器监督。超时记录保留人类可读 UTC `observedAt`，另存 `supervisorClock: monotonic`、监督开始后的 `monotonicElapsedMs`、`lastProgressAgeMs`，以及当前步骤的 `startedElapsedMs` / `ageMs`。这些相对量不使用系统日期或被测进程上报的时间计算。确定性回归命令：`node --test tests/acceptance/monotonic-supervisor.test.cjs tests/acceptance/runner.test.cjs tests/acceptance/timeout-diagnostics.test.cjs`。本修正未证明历史 preview.5 两次挂载超时的原因，不能据此关闭其缺陷记录。

结果包含操作完成和资源采样，资源/长稳准入必须独立判定；3 轮探针不代表 100 次重载、1,000 次生命周期或 8 小时浸泡完成。当前公开原生入口的 bootstrap 返回字符串，普通调用返回 Promise，页面按该实际契约等待响应。真实任务短探针也不替代高并发取消竞态、长时负载、旧版 Windows 与物理设备验收。
