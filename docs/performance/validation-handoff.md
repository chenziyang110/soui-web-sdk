# 双后端源码候选交接

本候选按用户“先完成代码，专人集中编译测试”的安排交付。PR #129 的主线底稿为 `0cdc0a9ace0cdce664ec4a9b54e5f8f71b97ba48`，旧冻结候选只提供增量来源。取舍与保留项见 [主线集成](main-integration.md)。既有预编译 DLL、LIB 和旧安装包没有本轮新增优化，禁止用它们的成绩证明当前候选。

## 方案落点

| 原方案 | 当前源码交付 | 仍需实际证据的条件项 |
|---|---|---|
| MiniBlink M0 | 基线快照、独立补丁/原始字节清单、三项体积门禁 | 实际包、运行目录、SDK/内核字节均不增长 |
| M1 调度/输入 | 保留主线 ready/deadline AVL 索引、mailbox/FIFO ordinal、ordered cancel 与 geometry-applied fence；增加截止唤醒及 worker 批次增量 | 单个 JS/GC 不可抢占；索引、取消、IME 和跨线程唤醒需用集成后源码验证 |
| M2 帧节拍/等待 | 分数帧周期、迟帧跳过、资源完成通知、拒绝通知的显式故障 | 实际 timer 精度、资源竞争、暂停与关闭 |
| M3 呈现 | 在主线双画布 lease、paint activity 与 mirror consumer 生命周期上协商按需镜像能力，兼容旧 DLL | 不回退主线画布和关闭所有权；真实像素、透明及回调重入仍需验证 |
| M4 光栅/脏区 | 新区域参与现有 8 区域最小成本合并，legacy 单次延后 | 可见 tile 优先级、opaque 清底、worker 并发须先定位热点 |
| M5 页面/分配 | WebView2/Scratch 下述公共宿主及可追溯页面候选 | 不改 Chrome49 的 JS/GC 语义，不凭空认定 Bridge/布局为主热点 |
| M6/M7 编译/GPU | 保留既有编译与软件路径，记录条件边界 | PGO、LTO 和实验 GPU 必须有代表性 profile、兼容与体积证据后才采用 |
| WebView2 P0 | 共用完整墙钟窗计量、GPU 身份/实际工作量能力、独立 raw W 工具、Electron 消费端 | E/W/S 的实际 Runtime/GPU/显示、电源、进程归属及 ETW 时间线 |
| P1/P2 宿主 | 相同 show/resize 的成功状态去重、重入保护、16项且2ms回复预算、锁外释放请求 lease | 不默认切换 Composition，不加 browser flags，不跨导航复用身份 |
| P3 Scratch | targets reducer 一次遍历、可选真实 step/draw 计数、面板变值写入、停止后排序；固定提交/lock 的源构建入口 | source candidate 尚未编译；虚拟列表、skin 共享、VM Worker 和调度重写由 profile 决定 |
| P4 响应/稳定性 | 输入终止保障和 COM/关闭生命周期修正，计量保留卡顿/零帧 | 实际输入、内存、启动、后台恢复与稳定性，不能由 rAF 代替 |

以上条件项属于完整方案的后续数据驱动选择，不代表当前代码已经取得收益。没有热点证据不将所有备选大重构同时启用，也不将“没有新增 DLL”写成零体积增长。

## 一次集中验证顺序

1. 冻结当前 commit、源码清单和工具链，分别准备 baseline/candidate 的独立输出。依赖源码/私有布局变化要求匹配重编 MiniBlink 与 SDK；保留实际加载路径及所有 DLL/页面/作品 SHA256。禁止混用旧内核。
2. 执行受影响源码回归，再验证新内核的输入、呈现和关闭。下面仅为交接命令，作者本轮未执行：

```powershell
node --test demos/scratch/web/performance-meter.test.mjs demos/scratch/upstream/targets-reducer.test.mjs tests/sdk/performance-size-gate.test.mjs tests/sdk/performance-zip-inventory.test.mjs
powershell.exe -File tools/Build-Dependencies.ps1 -Architecture x64 -Component Miniblink
powershell.exe -File tools/Build.ps1 -Architecture x64 -Configuration Release -WithVendorTests -WithWebView2
ctest --test-dir out/x64 -C Release -R '^(scheduler\.|kernel\.native\.(scheduler-fairness|worker-lifetime\.|fractional-frame|resource-notification|frame-buffer|render-|input-frame|independent-input)|sdk\.native\.(miniblink-frame-policy|webview2-capacity|host-adapter|local-editor-policy|local-resource))' --output-on-failure
```

构建入口若只产出依赖候选，须通过已有部署/身份入口显式选择该候选，确认 SDK 实际链接/载入匹配内核后才记结果。x86、Debug 与支持的兼容系统按此次影响单列；不可用一个架构结果外推。其他现有必需发布检查照原入口执行；不为本任务重跑无关历史长测。

3. 真实正确性关注：队列满时 down/up、blur、中文 IME，多 Host，geometry/document 变化；透明、resize/DPI、DC 新鲜度、DIRECT_HWND/legacy；资源完成/拒绝通知、初始化取消、COM 重入、导航、DevTools 暂停/继续与关闭。针对失败只重跑受影响集合。
4. 使用 [体积门禁](size-gate.md) 冻结同功能/资源/工具链/打包参数，ZIP、解压运行目录、SDK/内核三项均不增大。源码构建的 Scratch 是显式候选，默认 npm artifact 仍为对照；两端必须选择同一载荷。未通过门禁不可替换正式包。
5. 页面候选构建见 [Scratch 源构建](../../demos/scratch/upstream/README.md)。E/W/S 共用冻结资源，W 入口见 [raw WebView2](../../experiments/scratch-webview2-baseline/README.md)，E 入口见 [Electron](../../demos/scratch/electron-comparison/README.md)。正式新口径使用整窗 `windowRafFps`；无 rAF 窗口计 0，头尾缺帧被截断的分位数不作通过依据。真实卡顿保留，不能丢弃后自动重采。
6. 先取得一个短 CPU/图形 profile，决定条件项；正式计分关闭 profiler。相同设备/Runtime/GPU/页面/作品/工作量/显示条件下按原方案有限交替配对，报告每轮分布、实际逻辑推进和有效性。MiniBlink 使用其与 Electron 均兼容的负载，不能借用现代 Scratch 的 WebView2 数字。

原方案的目标仍是同负载 S/E 整窗回调率至少95%、P95/P99及输入尾延迟至多110%，S/W回调率差至多3%；CPU、进程组内存和可交互启动逐项报告。它们是待验收目标。历史 42.861/51.866 仅为旧口径观察，不能作为新口径达标证据；rAF不等于桌面呈现或物理输入到发光延迟，QEMU也不提供物理高刷证明。

## 评审和回退

ADR 0138–0143、各 lane 交接及 MiniBlink 0024/0025 补丁组成当前设计与来源记录；主线原有 0016/0017 等补丁不覆盖。回退时保留匹配的一组 SDK、内核和页面；不得只换某个私有布局相关 DLL。Scratch 可继续选原锁定 artifact。独立静审解决的是源码可见缺陷，编译、正确性、大小及性能分别记录，均不由静审代替。
