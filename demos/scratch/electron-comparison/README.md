# Electron 对照测试

测试包装直接只读加载已打包的 Scratch `resources/`，不替换编辑器、项目配方或计时器。
这是页面执行和渲染的对照工具；preload 只提供页面启动需要的测试响应，不实现正式 SDK Bridge，不能测试原生文件操作。

从官方 npm 注册表在仓库外安装固定 Electron 版本，然后从 SDK 根目录运行：

```powershell
& <Electron安装目录>/node_modules/electron/dist/electron.exe demos/scratch/electron-comparison/main.cjs --resources <r4包>/resources --baseline <用户性能报告.json> --output <不存在的证据目录> --runs 3
```

窗口打开后点击“性能实验”，选择“重负载”，点击“载入并运行”并确认。
测试自动核对项目 SHA256、241 个 targets 和 240 条脚本，保持用户报告中的内容尺寸与 DPR。
每轮先预热 10 秒，再用原计时器采样 30 秒，共三轮。测试期间保持窗口置前，不操作、不截图、不运行另一份动画负载。
失焦、最小化、隐藏或调整窗口大小会留下失败证据。计时器本身也会拒绝页面隐藏的记录。
不关闭硬件加速，不设置特殊帧率参数，不启用 DevTools。结束后停止脚本并保留窗口。

输出保留用户报告原始字节及 SHA256、页面和包装源码 SHA256、Electron/Chromium/OS/CPU/GPU 信息、每轮原始报告和 `session.json`。
完成后三轮采集有效且 `session.json` 的 `pass` 为 true，运行 `node demos/scratch/electron-comparison/summarize.cjs <证据目录>` 生成对比 JSON 和 Markdown。`pass` 仅表示采集／环境有效，不能当作性能通过。完成但卡顿或零帧的轮次必须保留；失焦等环境失败不进入正式汇总，原始记录仍保存。

此候选要求资源包含 `measurementVersion:2` 计量。正式回调率使用 `windowRafFps`（实际完整墙钟窗分母），原 `rafFps` 保留为旧闭合间隔 cadence。首／尾缺帧超过开始前声明的两帧预算会标 `censored`，主分位数为 null，已观察间隔值放在 `observedFramePercentiles` 供诊断。`validForWindowRateComparison` 可接受可见的真实零帧窗口并计 0；不能把卡顿当无效轮次丢弃重跑。旧历史报告没有新字段，汇总会保留其未知状态，不宣称新口径已对齐。
与用户提供的历史单轮样本比较时，必须说明双方采样次数、预热与当时系统负载的未知差异；不能把差值直接归因于 SOUI 原生层。
rAF 表示页面回调节奏，JS 堆不包含整个进程或 GPU 内存。

## 单次诊断消费者

在上述新输出命令加 `--diagnostic profile --runs 1`，固定消费者会沿原场景按钮和确认框自动加载同一 heavy 项目，并执行共同的 10 秒预热/10 秒 CPU+browser profile。历史 `--baseline` 文件在该模式只提供视口与 SDK 启动身份，不能作为新诊断的旧成绩分母；实际工作量与资源身份由新报告核对。

驱动使用本机 `webContents.debugger`，不打开 DevTools UI、远程端口或改 sandbox/GPU/backgroundThrottling。共同配方和 probe 位于 `experiments/scratch-webview2-baseline/`，输出会记录其真实字节 SHA。`diagnostic-driver.cjs` 只发送固定 Profiler/Tracing 命令、执行固定页面脚本；一般网页没有新增诊断 Bridge 方法。

完整 `diagnostic/` 原件和 collection、CPU profile、trace、page guard 等资格分别保留。丢失数据、缺 category 或诊断能力不可用不代表该阶段成本为零。profile/16ms timer 有开销，分析用 page marks 的实际窗，不能把这些数记为正式 FPS。原生全程 foreground/occlusion 和同时间轴 OS/GPU trace 由统一外部调度者采集；只在它确认机器安静后启动。诊断完停止新消费者的 VM、记录未 settled 请求、detach 本次 debugger，正常关闭这个新窗口并 app.quit；这些操作不冒充底层请求已取消。外部 Owned 记录还需证明自然退出和进程组闭合。正常三轮模式仍保留窗口，已有手动对照窗口不关闭或覆盖。
