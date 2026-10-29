# WebView2 / Scratch 性能候选交接

日期：2026-10-29。基线：`c17c79bdf470fae681c63324ceaa2a519151afc9`（包括原工作树的 DevTools / Scratch 源码快照）。本文件仅说明当前 lane；全局体积与 MiniBlink 集成由负责人汇总。

## 已实现，待专人编译与运行

- `sdk/renderers/webview2/owner_work_policy.h`：Controller 成功状态去重与失败/重入失效。`webview2_host.cpp` 的首次 bounds、后续 resize/show 使用该策略；验证参数、mounted/owner STA 的检查继续执行。
- show/resize 与首次 mount 在 COM setter 调用期间分别保留 state 与 Controller 的强所有权；mount 的资源映射和首次 Bounds 返回后还要求 phase 仍为 prepared 且 controller/core/core3 身份一致，否则直接 E_CANCELLED，不挂 Bridge/worker 或复活 closed 状态。回复使用局部 core 强引用并在可重入 COM 边界后重新核对 phase/core/document/generation。取消、导航与销毁不能沿用刚才成功的旧身份。
- 回复批次保留最多 16 项，再受 2ms 合作式预算约束。第一项可进展；单次 JSON/COM 不可抢占。请求 lease 在 Host mutex 外释放。每输出仍检查 generation/document/source，未跨导航缓存身份。
- Meter 容量上限 120002，支持最长 120s×1000Hz；结束时排序一次，轮询 snapshot 不反复排序。滚动范围明确保留为诊断且不能 `validForComparison`。旧用户 JSON 不变。
- 源码能力 `runtime.souiPerformanceCounters` 存在时按记录期间增量收集 steps/draws；原始包无能力时值为 null。目标通知是独立计数，不能替代 VM steps。计数对象只在原为 null 时由本 meter 持有；同 Runtime 的重叠 meter 共享租约，最后一位停止/重启/销毁后才释放，不清除外部 observer 持有的对象。
- 在已有 WebGL context 中一次读取有界图形身份，不创建额外 Canvas/context。性能面板不可见时不写 DOM，可见时仅变值写入；分位数显示为结束后统计。
- 原生策略与 JS 生命周期、高刷完整窗口、排序、截断、工作量所有权的回归源码已加入。
- `measurementVersion:2` 的 `windowRafFps` 覆盖完整实际墙钟窗，`rafCallbacks`、`leadingGapMs`、`trailingGapMs`、`boundaryBudgetMs` 一同保留；旧 `rafFps` 仍是闭合间隔 cadence。超出开始前声明的两帧预算或零回调标 `censored`，主 P50/P95/P99 为 null，`observedFramePercentiles` 仅供诊断。可见且完成的真卡顿与零帧结果仍通过 `validForWindowRateComparison`，以低回调率保留，不能丢弃重试来美化。
- Electron wrapper/summarizer 使用整窗 gate；采集 pass 与分位数资格独立。历史单轮基线缺少新字段时只保留参考，不补造完整时窗数据或性能通过。面板 stopped/idle 分支清除此前完成状态。

## 静态检查

已执行：`node --check demos/scratch/web/performance-meter.js`、同目录 test/app 的语法检查，以及相关路径 `git diff --check`。未运行任何测试、构建、VM 或桌面采样；这些检查不证明运行正确性或性能收益。

## 专人集中验证入口

从 SDK 根目录：

```powershell
node --test demos/scratch/web/performance-meter.test.mjs
node --test tests/web/webview2-preload.test.mjs
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -WithWebView2
powershell.exe -File tools/Test.ps1 -Architecture x64 -Configuration Release -UnitOnly
```

`webview2_request_capacity_tests.cpp` 的已有原生目标包含新增 2 项策略案例。实际 HWND/COM、重入/导航/关闭、重复 resize/show、持续回复下输入公平性及资源授权回归仍须真实运行；只对受影响集合补测。

Meter 运行中 P95/P99 为 null，结束后固定；censored 时主分位数仍不可用。`validForComparison` 要求完整间隔窗口且无超预算边界，而整窗回调率资格为 `validForWindowRateComparison`，两者不可互换。既有消费端不能再依靠记录中的分位数做判定。报告 v1 的旧字段含义保留，并要求新工具识别 measurementVersion；新增字段可被旧消费者忽略。显式 `includeIntervals` 会生成较大报告，仅供工具写本地原始记录，默认原生保存仍不含明细。

## 未采用的条件项与范围

没有本候选 profile，不推断 GPU/controller、VM/纹理/GC 是主瓶颈。Composition controller、VM Worker、跨纹理重排、skin 共享和通用消息身份缓存未默认采用；仍按完整方案保留待证条件。上游 Scratch 源码 patch/build pipeline 和 E/W/S 基准由集成负责人拥有，本 lane 不直接修改缓存、dist 或打包 JS。

无新增第三方运行依赖；代码改变仍可能增加产物字节。包体、进程内存、CPU 和前后 FPS 均未验收，必须通过共同零增长门禁后才能采用。当前不宣称已对齐 Electron。
