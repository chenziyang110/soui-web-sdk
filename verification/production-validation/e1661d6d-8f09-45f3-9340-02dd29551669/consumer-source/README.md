# 公共 SDK 独立原生测试应用

本工程通过已安装 SDK 的 `find_package(SouiSdk)` 构建，只使用公开头文件和 API。原生 Win32 父窗口由应用提供，WebView、引擎线程、Bridge、会话与关闭行为由 SDK 提供。它不依赖模板、Demo 或 SDK 私有实现。

从交付根目录运行：

```powershell
powershell.exe -File tools/Test-Acceptance.ps1 -Architecture x64 -Scenario all -Cycles 3
```

该入口编译 SDK、安装到新目录、独立编译本工程、部署本次载荷并串行运行三个场景：

- `reload`：同一 WebView 多次重载，每轮重新完成原生连接和窗口身份检查。
- `recreate`：同一进程销毁窗口/Host，再创建新的窗口/Host。
- `multiwindow`：三个窗口同时存在，关闭中间窗口后重载另两个窗口，再创建中间窗口并退出。

`-Cycles` 是被测轮数，`-Warmup` 是额外预热轮数；每轮按照 `-SettleMs` 等待后采样。`-SkipSdkBuild` 只接受源码和 SDK 载荷仍与最近 SDK-only 构建记录一致的情况。测试编译输出和每次运行的原始记录保存在新建的 `out/acceptance/<suite-id>/`，失败样本不覆盖。

每个 suite 的 `consumer-source/` 保存实际编译的独立消费者源码，`kernel-input/node.dll` 保存本次测试的内核输入；v2 构建记录包含两者散列，运行前后均验证。默认使用固定 prebuilt；测试新编译内核时可显式指定 `-RuntimeBinary out/dependencies/x64/miniblink/bin/miniblink.dll`。候选只复制到新 suite 并记录为 `explicit-candidate`，不会替换固定依赖。旧 v1 记录继续保留为历史证据，新的运行需要重新构建 v2 suite。

`node --test tests/acceptance/runner.test.cjs` 验证结果核验器拒绝崩溃、超时、缺失或重复步骤、错误窗口、参数不符和缺失采样。采样必须出现在对应步骤完成之后，提前记录 closed 样本不能通过。核验器通过本身不证明真实 SDK 场景通过。

结果包含操作完成和资源采样，资源/长稳准入必须独立判定；3 轮探针不代表 100 次重载、1,000 次生命周期或 8 小时浸泡完成。当前公开原生入口的 bootstrap 返回字符串，普通调用返回 Promise，页面按该实际契约等待响应；取消、事件和完整释放能力仍需独立验收。
