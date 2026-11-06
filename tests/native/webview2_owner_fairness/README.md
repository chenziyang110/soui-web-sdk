# WebView2 原生输入与 owner 公平性测量源 v2

本目录接回 R45 修正、R50 构建、R54 实际执行过的独立消费者源码，并增加正常公共 SDK owner-loop 接入。12 份运行文件保留来源原字节；`main.cpp` 调整循环接线、共用取消息观察、共用冷可见性设置与结果范围元数据，新增 `message_retrieval_snapshot.h`，来源与差量见 `provenance.json`。**没有配置、编译、运行23项 policy 或采样**。R54 已有实际执行是失效诊断，不能继承为本目录通过：3125 个正 tag 最终全部收到，但严格窗口出现未标记回调，ACK500失败、页面记录 hidden，整体性能与公平性仍为 false。

## 构建身份与边界

这是独立 CMake 工程，不自动加入仓库默认测试，不自动启动窗口或输入。必须显式给出 `MATCHED_SDK_ROOT`（绝对安装 prefix）、`MATCHED_SDK_VERSION`（完整 raw release）、唯一 `SOUI_FAIRNESS_BUILD_ID`（消费者身份）及 `SOUI_FAIRNESS_OWNER_LOOP=legacy/sdk`（本轮模式）。没有 r50、SDK5、历史 BuildID 或默认循环模式。复用 `benchmarks/input-ui-host/sdk_version.cmake`，检查 numeric EXACT package、实际 imported `version.h` raw 以及实际 prefix；外部最小源副本须保留该相对 helper，或显式设置 `SOUI_FAIRNESS_VERSION_HELPER`。

从仓库根配置时，将下面四个环境变量设置为本轮真实冻结值，继续使用现有构建准入、标准 v143 生命周期与 owned runner；不要在并行编译占用期间直接启动 CMake：

```powershell
cmake -S tests/native/webview2_owner_fairness -B out/webview2-owner-fairness-x64 `
  -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_VERSION=10.0.19041.0 `
  "-DMATCHED_SDK_ROOT=$env:SOUI_MATCHED_SDK_ROOT" `
  "-DMATCHED_SDK_VERSION=$env:SOUI_MATCHED_SDK_VERSION" `
  "-DSOUI_FAIRNESS_BUILD_ID=$env:SOUI_FAIRNESS_BUILD_ID" `
  "-DSOUI_FAIRNESS_OWNER_LOOP=$env:SOUI_FAIRNESS_OWNER_LOOP"
```

目标保留 `SouiWebView2OwnerFairness`（2 TU）、`R41NativeStripInput`（1 TU）和 `R41PolicyContracts`（原23项，1 TU）。此处不再附带构建容量浏览器回归；它继续由 `tests/native/webview2_capacity_host/` 独立持有。保持 C++17、Windows 7 宏与实际 Windows SDK19041工具链，不提高最低系统。`GetCurrentInputMessageSource` 仍为可选动态解析，仅诊断来源类别。

Host 只链接正常 imported `Soui::WebView2` / `Soui::ActionJson`，通过正常 SDK 的公共 diagnostics recorder 工作；**不再编译一份 recorder.cpp、不强制多重定义、不复制 SDK 实现**。`native_burst_marker.h` 是有来源的 inline wire helper，不是另一份 SDK recorder；其编码与现有诊断来源一致。`resources/index.html` 保留原 CRLF，局部 `.gitattributes` 防止 Git 改写其冻结字节。

生成的 `sdk-identity-<配置>.txt` 记录 raw/package、唯一消费者 BuildID、实际头文件 hash 及 SDK/WebView2/Loader 三库解析路径。可显式传 `SOUI_FAIRNESS_SDK_BUILD_RECEIPT`，只冷记录正常 SDK 构建收据的路径、字节数和 SHA-256。安装头/config 未提供真实 SDK BuildID，因此这里始终写 `sdk_build_id=unknown`、`sdk_build_provenance_verified=false`；提供一份任意收据不会变成通过。最终仍由现有冻结 runner、实际链接记录、库 hash 与正常 SDK 收据闭合版本、架构、配置和 BuildID。没有该闭合不授予构建来源或性能资格。

## 公共 loop 接入与两侧仪表

`sdk` 模式只使用正常安装 SDK 的公共 `windows_owner_loop`：在任何 Host prepare 前构造，让适配器按正常契约登记，再用 `run(callbacks)` 驱动。没有复制 SDK 私有 dispatcher、调度器或回退实现。真实公共头不存在时配置失败，不生成替身。`legacy` 模式不构造该对象，保留普通 GetMessage 路径，因此也可用于未提供新 API 的旧 SDK；它是对照模式，不能替代最终 SDK loop 采用。

两侧共用冷设置在 `start/resize` 成功后显式检查公共 `host.show().ok`，随后仍按原方式显示父窗口。旧 M01 设置没有显式调用 Host show，仅显示父窗口；这个遗漏不能推导为“SDK 默认隐藏”或唯一解释既有约29秒间隔。本修正不改 SDK start 语义、不强行 focus Web 页面。show 成功只确认公开调用被接纳，不能代替实际 DOM 可见性：页面 phase/fence 原有 `visibilityState/hidden/hasFocus` 继续保留并分别判断。原生 strip 的输入焦点与页面焦点也不混用，旧 R54 原件不回写。

两模式共用 `message_retrieval_snapshot` 和实际 dispatch 回调。SDK 模式在公共 `message_retrieved` 被动钩子内，legacy 模式在实际 GetMessage 返回后立即调用同一观察体：仅对真实 strip 的 WM_MOUSEMOVE 先取 extraInfo，随后取 QPC 并保存实际 MSG。观察体不取下一条消息、不派发、不建立 guard，不递增应用 retrieval ordinal。实际 dispatch 才核对完整语义字段并建立原 RAII guard；输入时差沿用早期 QPC，不能在 SDK 服务之后补一个时间冒充即时检索。丢失/错配保持 unknown 并使本轮失效。SDK 自己消费的 private 消息没有应用 guard/ordinal；未观测的 nested 检索仍不能借用外层上下文。

最终 JSON 增加 `ownerLoop`、`messageRetrievalObservation` 和 `applicationPumpObservation`，raw29列与原 profile/schema 不变。loop 优化器状态与正常窗口循环可用性分开记录；`legacy` 的优化器状态为 null，`sdk` 保存公共 status 的实际前后值。两侧必须使用相同新 observer 源和匹配配置，不把旧 R54 样本当同仪表对照。

原 `phases.*.pumpEntries/maxPumpCallQpc/crossPhasePumpReturns` 只观察应用转发 `webview2_event_message` 的 legacy Host wake handler。SDK 内部 continuation 不经过该 handler，因此这些值不能当全部 SDK owner pump 的总数或最大间隔；新增 metadata 明确额外 SDK pump 次数为 null/unknown。需要全 SDK 阶段时由现有公开 trace 与冻结读取器另行核验，不根据应用计数补值。

## 不变的测量契约

- 使用原 `r45-native-drain-strip-v2` 与29列 raw ledger。tag0、错序、tuple/extra/坐标/线程/时钟错误永久失败；所有额外回调保留到 Host 终态，不过滤、不裁样，也不把消息来源类别当 sender PID 认证。
- SenderDone 只表发送阶段结束；ReceiverDrain 要求1..3125真实正 tag 全部顺序接收并经 owner 安全点封口；Finish 仍是发送器最终 attempt 结果持久化后的终态。三个事实分开，严格身份窗口不会在封口时提前结束。最终软件 marker 必须覆盖全部实际接收；不新增 capture 必须晚于行政 seal 的门。
- 原30秒4 KiB Bridge、7 echo与至多1 control；25秒3125@125Hz、一次29秒因果 fence；页面冷输入准备25秒、READY15/GO30、Host70+20/绝对90秒、input45、drain2和ACK500全部不变。ACK500在 SenderDone API 前固定，API耗时计入，不能为了等晚收而延长。
- 原 Default/nonremote/input-desktop、实际 PID/birth、唯一 HWND/child、foreground、geometry、session、Esc、固定 runtime/正常关闭及自然 owned Job 门仍由原代码和冻结执行入口共同要求。此目录没有绕过 GO 的启动脚本或 GUI CTest。

应用仍只接受原显式 `--profile/--resources/--output/--runtime/--run/--ready/--interaction-profile` 参数；输入器仍要求本轮实际 PID/birth/HWND/run/nonce。不得直接双击或使用旧入口/旧 image pin 给新构建授权。后续入口集成由任务负责人使用现有 runner 完成，本次没有新建采样平台。

## 结果的范围

原 schema、窗口类名、事件名以及部分 `R41`/`SDK4` 历史描述字符串保留。它们不决定当前 SDK 身份；以新的 `sdkRawVersion`、显式 `buildId`、循环模式、实际载荷和冻结输入为准。也不得把这些旧文本、R54运行记录或23项 policy 的历史结果挂到本目录的新产物上。

页面 `visibilityState/hidden/hasFocus` 保留原采样点；原生 HWND 前台检查与页面可见性是不同事实。软件 marker、WM_PAINT/WM_TIMER次数和 QPC 等待都不等于物理帧率或输入到显示延迟。CPU/峰值 Job commit 也不等于完整应用/GPU/工作集内存；缺失值继续 unknown/null。失效 cohort 可定位问题，不能把其分位或单轮差值宣称为验收或性能收益。
