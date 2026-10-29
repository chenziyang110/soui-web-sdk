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
完成后三轮均有效且 `session.json` 的 `pass` 为 true，运行 `node demos/scratch/electron-comparison/summarize.cjs <证据目录>` 生成对比 JSON 和 Markdown；失败样本不会进入汇总。
与用户提供的历史单轮样本比较时，必须说明双方采样次数、预热与当时系统负载的未知差异；不能把差值直接归因于 SOUI 原生层。
rAF 表示页面回调节奏，JS 堆不包含整个进程或 GPU 内存。
