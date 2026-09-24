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
- `bridge`：公共连接器、并发关联、原生等待、容量上限和错误恢复。

`-Cycles` 是被测轮数，`-Warmup` 是额外预热轮数；每轮按照 `-SettleMs` 等待后采样。`-SkipSdkBuild` 只接受源码和 SDK 载荷仍与最近 SDK-only 构建记录一致的情况。测试编译输出和每次运行的原始记录保存在新建的 `out/acceptance/<suite-id>/`，失败样本不覆盖。

每个 suite 的 `consumer-source/` 保存实际编译的独立消费者源码，`kernel-input/node.dll` 保存本次测试的内核输入。v3 还复制同次构建的公共 Web SDK 到 `web-sdk/`，使用 `tests/acceptance` 自己锁定的 Vite/legacy 工具链生成 `web-output/`；C++ 仍只引用安装的公共 SDK。构建和运行前后核对消费者、Web SDK、Web 输出、原生安装与最终载荷。默认使用固定 prebuilt；测试新编译内核时可显式指定 `-RuntimeBinary out/dependencies/x64/miniblink/bin/miniblink.dll`。候选只复制到新 suite 并记录为 `explicit-candidate`，不会替换固定依赖。旧 v1/v2 记录继续保留为历史证据，新的运行需要重新构建 v3 suite。

`-Scenario bridge` 通过真实公共连接器验证重复连接复用、32 个请求身份关联、200 ms 原生延时期间页面计时器继续推进、业务失败、超大响应与随后正常调用。客户端容量特意高于 Host，通过 257 个普通请求检查原生 256 上限；普通槽位占满时的取消入口应进入原生校验并返回 `E_UNKNOWN_METHOD`，验证控制容量存在。它没有实现公共任务服务，不能据此声称支持取消，也不能代替真实事件和直接原始入口的完整验收。`all` 包含此探针和三个生命周期模式。`-PrepareOnly` 只编译并记录身份；退出码 0 表示准备成功，不表示执行场景通过。

`node --test tests/acceptance/runner.test.cjs` 验证结果核验器拒绝崩溃、超时、缺失或重复步骤、错误窗口、参数不符和缺失采样。采样必须出现在对应步骤完成之后，提前记录 closed 样本不能通过。核验器通过本身不证明真实 SDK 场景通过。

监督器一旦判定超时，会先写入 `timeout-observation.json`：超时原因、最后已解析事件、已读字节数、当时日志长度及末尾内容。需要在终止前抓取线程现场时，可选地设置 `SOUI_ACCEPTANCE_DEBUGGER` 为与被测进程同架构的 `cdb.exe` 绝对路径，并设置 `SOUI_ACCEPTANCE_DUMP_ROOT` 为有足够空间的仓库外绝对目录。监督器以本地符号路径附加，最多等待 20 秒，在该目录的 run-id 子目录保存完整 dump 和线程栈日志；结果中记录状态、长度及 SHA-256。未配置或抓取失败仍按原超时判失败并终止进程，不能将 dump 视作测试通过。dump 可能包含进程内数据，不进入公开源码包；本验收夹具仅用确定性测试数据。`node --test tests/acceptance/timeout-diagnostics.test.cjs` 默认测日志偏移；设置 `SOUI_TEST_CDB_X86` 和 `SOUI_TEST_DUMP_ROOT` 后会另用临时 x86 进程验证真实抓取链路。

结果包含操作完成和资源采样，资源/长稳准入必须独立判定；3 轮探针不代表 100 次重载、1,000 次生命周期或 8 小时浸泡完成。当前公开原生入口的 bootstrap 返回字符串，普通调用返回 Promise，页面按该实际契约等待响应；取消、事件和完整释放能力仍需独立验收。
