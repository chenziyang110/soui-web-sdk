# Scratch 最小 raw WebView2 对照宿主（W）

此目录是本地性能实验消费者，直接创建 WebView2 Environment/普通 HWND Controller，不链接或创建 `Soui::WebView2`、SDK Host、正式 Bridge worker 或业务服务。它用于把同一 Scratch 页面中的 SDK 增量与 Runtime/页面差异分开。不是发布后端，不代表正式 SDK、安全或生产生命周期验收。

当前为源码候选，**未编译、未启动、未采样**。存在可执行构建/采集入口不等于 E/W/S 三层对照已经完成。

## 构建与运行

命令从 SDK 根执行，由专职验证人员在代码冻结后集中执行：

```powershell
powershell.exe -NoProfile -File experiments/scratch-webview2-baseline/Build.ps1 -Architecture x64 -Configuration Release
& .\out\scratch-raw-webview2-x64\bin\Release\ScratchRawWebView2Baseline.exe `
  --resources C:\bench\same-scratch-resources --output C:\bench\raw-wv2-run-01 `
  --width 1424 --height 861
```

`--resources` 必须指向 S 与 E 使用的同一套完整已部署 `resources`；不要重新生成不同页面后比较。目录预算沿用此 Scratch Demo：最多 4096 文件、单文件 32 MiB、总量 256 MiB；拒绝不可读文件、链接与 reparse point。输出目录必须全新且与资源目录互不包含；其父目录必须已经存在，启动时先解析该父目录的 junction，再检查边界，创建后再核对实际路径。profile 放在输出目录下并要求独占访问；旧证据与 profile 不覆盖、不删除。验证期间冻结这些目录及父目录，运行中替换目录或资源不属于本工具的文件系统防篡改保证。

Build 只通过当前 SDK 的 `Restore-WebView2.ps1` 取已固定/校验的包和工具链。不构建整个 SDK、不运行任何测试、不下载网页资源。CMake 手工使用时也必须传相同已校验 `SOUI_WEBVIEW2_SDK_ROOT`。实际使用 Evergreen Runtime，具体版本记录在 `session.json`；先与 S 的真实 Runtime 对齐。

## 有限采集

1. 保持窗口前台完整可见，在原页面打开性能面板，选相同场景和级别并载入；不要点击页面“记录”按钮。
2. 按 **F6**。原生 Accelerator 将操作投递回 UI 消息泵，再执行同页面已有 meter：固定预热 10 秒，然后记录 30 秒。不修改页面文件、VM 逻辑、画质或执行步频。
3. 等待标题显示 sample saved/invalid。输出保留原始 `webview2-run-N.json` 与独立 `session.json`；最多三个采样，不自动重试。原生失焦、最小化、resize、超时、远程请求、页面无效会使该实验 cohort 不通过。采样期间不截图、录屏、跑其他动画或桌面自动化。采集要求 `measurementVersion:2`、`validForWindowRateComparison:true` 和有限非负的 `windowRafFps`；完成时窗内真实停顿、零回调和边界未闭合仍是有效的差成绩，必须保留，不能按环境失败丢掉或重采。
4. 三层比较还需核对场景/作品 SHA、完整资源清单、目标/线程与逻辑步数、真实 GPU、显示/电源条件，并按共同协议审查。主回调率使用完整实际时窗的 `windowRafFps`；历史 `rafFps` 只描述已闭合间隔的节奏。`censored` 会令正式分位数为空，独立的 `percentilesQualified` 反映分位数资格，不能冒充尾部停顿已被测量。`session.pass` 仅表示此次采集守卫结果，不能直接表示追平 Electron。原始报告的 valid 字段保留原值；原生窗口失效与它分别报告。

首轮工具限定 **DPR 1**，默认页面内容尺寸 1424×861；F6 会拒绝尺寸、DPR 或场景在预热期间变化。窗口激活事件守卫覆盖整个预热＋采样区间，比单独采样更保守。高 DPI/旋转/远程显示、Win7 等并未由本工具声明支持，不能外推。重复测量需由专人决定受影响范围，不无限增加轮数。

## 保留的来源和加载边界

- 使用同一 `https://soui-webview2.invalid/index.html` 和同样的 VirtualHostNameToFolderMapping/DENY；所有 source kinds 的资源过滤器仅允许这一 HTTPS origin，其余返回 403。
- 顶层只接纳一次受控入口；子框架只允许 `about:blank`/`about:srcdoc`，对应 Scratch 使用的显式 local-editor 档位。新窗口、权限、下载、默认 dialogs/context menu、DevTools、host objects 均禁用。
- 复用 SDK `loader_policy.h`，拒绝进程环境/策略参数覆盖，不传 GPU/VSync/沙箱/站点隔离等宽松 flags。
- 嵌入 SDK 同一 `native-preload.js`，只给顶层受控来源装 binding。仅响应 bootstrap、app.info、probe；检查来源、document/session/token 和消息大小/深度。其他原生业务请求返回不支持。因此保存工程/页面原生导出按钮不作为此 W 的功能，采样用 F6 写入工具自己新建的证据目录。
- 该最小 transport 用于可信测试页面，不是完整 SDK 的授权/续租/撤销/任务服务实现。加载网络边界没有为跑分放宽；业务服务的缺省是明确实验差异，不能用 W 成绩证明正式 SDK 安全。

## 身份和关闭边界

记录 EXE 与六个主要页面资源 SHA256；另以排序后的完整 `resource-inventory.json` 列出资源目录每个文件的相对路径、大小与 SHA256，并将该清单文件 SHA256、文件数和总字节数写入 `session.json`。同时记录实际 Runtime、host/browser PID 与 browser 创建身份，以及原生失效事件。资源应在验证前冻结；这些是启动时的资源身份，本工具不是离线签名器，也不声称防止运行期间资源被其他进程篡改。预热/采样页面报告带各自环境与工作量信息，GPU/ETW 归因仍需未来专职定位。

正常关闭保留消息泵，等待初始化回调、关闭 Controller，再等待已接纳 `ExecuteScript` 的实际原生完成回调排空及本工具独占 profile 的 browser 进程句柄实际 signaled；`nativeCallbacksPending` 分别记录初始化与脚本剩余数。脚本中异步页面工作不冒充原生回调，关闭采样中窗口会使该次采集失败。初始化中取消没有完整 browser 身份时明确保留未通过/未知清理范围；超过十秒写失败并以退出码 3 终止工具，不把强退当清理成功。正式 SDK 的完整 callback/环境退出观察与恢复契约不由这个实验替代。

工具启动前的资源枚举与 SHA 读取会改变文件缓存，不能拿该 W 启动时间直接证明正式 SDK 冷启动增量。这里首先支持同载荷的稳态 rAF 对照；屏幕呈现与物理输入延迟需要另有证据。
