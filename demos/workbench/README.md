# Workbench Demo

## 原生 SOUI 控件覆盖 WebView

构建当前 Workbench 后，从 SDK 根执行 `powershell -File demos/workbench/Start-NativeControls.ps1`。
独立窗口左侧为真正的 SOUI `edit`、`check`、`sliderbar`、`button` 控件，右侧为本地 HTML 输入框和计数按钮。
可分别输入中文、点击计数、勾选、拖动滑块和滚动网页，体验原生与 Web 焦点切换。

此入口使用 `--native-controls` 与独立本地资源目录；不改变默认 Workbench 页面。
原生面板是 WebView 子窗口上方的同级 SOUI 子窗口，固定在客户区左侧，不参与 DOM 排版或页面滚动。
这是 MiniBlink 宿主的不透明原生浮层演示，不代表 WebView2、透明合成、DOM 锚定、跨屏 DPI 或完整键盘导航已验收。
启动脚本需源码中的 `native-controls/index.html`，不是可脱离源码独立发布的安装包。

当前演示状态（2026-10-28）：x64 Release 编译通过；已实际观察原生面板与完整网页同时显示，原生计数、滑块、网页计数和网页输入焦点反馈。
空白原因是旧内核的公共 broker endpoint 关闭了 CPU AutoDraw。演示内核从保留的链接输入和 broker 源码重编，显式区分业务窗口绘制与 Inspector callback-only；不覆盖发布依赖。
本机启动器优先使用 `out/native-controls/x64/node.dll` 的演示候选，也可用 `-Miniblink <匹配内核路径>` 显式选择。
可双击 `demos/workbench/Start-NativeControls.cmd`。演示结果不等同于当前完整 SDK 内核、DevTools 或发布矩阵验收。

展示媒体、文件扫描、组件与桌面能力。integration/ 中的方法是 Demo 业务适配器，不属于通用 SDK API 保证。

Workbench 前端从 0.4.0-preview.3 起要求 Node.js 22.12 以上进行构建，原因是 Element Plus 2.14.7 使用 VueUse 15。基础 SDK 和兼容生成项目仍可按原要求使用 Node.js 20.19；Windows 应用运行时不需要安装 Node.js。最低系统和 Chrome 49 兼容构建目标不变，新依赖仍须按目标环境独立验收。

从交付根目录执行 tools/Build.ps1，运行 out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe。默认使用稳定的软件呈现路径；`--renderer gpu`、`--renderer readback` 和 `--renderer auto` 仅用于 GPU 内核实验，当前不属于生产验收范围。自动验证使用 tools/Test.ps1。新业务项目使用 templates/desktop-app/。

## V02 数据交互与持续任务

交付根运行 `out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe --react --page=data`，x86 使用对应输出。万条列表旁保留中文组合事件、窗口内容尺寸及 RAF 诊断；页面提供原快速计数任务和新的“运行 30 秒任务”，可在任务期间滚动、使用真实中文输入法、调整窗口并取消任务。

持续任务由 Demo 注册 `demo.interaction`，通过公共 `task.start/progress/query/cancel` 调用。唯一输入 `durationMs` 接受 1,000–60,000 的整数；以原生单调时钟计时，协作式取消，返回实际 `elapsedMs` 与迭代次数。调度可能延迟完成，时长不是响应承诺。它是低负载的交互伴随任务，不是高 CPU 压力或吞吐基准。页面分别显示实际收到的进度事件与成功查询次数，订阅失败明确显示；查询不会被计作事件。

React 页面的外层和 V02 列表滚动条使用 Chrome 49 兼容的 `::-webkit-scrollbar` 样式；这只改变轨道、滑块和箭头外观，不改变滚轮、拖动或键盘滚动行为。Win7 x86 上的实际显示与输入仍须用该次构建单独验收。

这是 Demo 业务能力，不增加公共 SDK 契约，也不修改已冻结的 preview.15 SDK Core。修改后的 Workbench 必须重新构建、冻结并执行各目标平台验收，既有同版本应用短测不覆盖此 Demo 改动。原生任务回归和页面构建均不能代替真实滚动、IME 候选/组合/提交、DPI、多屏、关闭及长稳；不能把脚本赋值或 Unicode 注入当作 IME 输入。

## 显式三窗口驻留（preview.10）

交付根运行 `out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe --resident --react --page=data`。应用控制器创建三个独立 Host/adapter/会话，共用进程 Runtime；关闭任一业务窗口或全部业务窗口后仍保留托盘。点击图标、托盘菜单或 Ctrl+Alt+W 可恢复窗口，托盘菜单提供明确退出。快捷键被占用或服务初始化失败会记录错误并退出，不静默换用其他组合。默认不带 `--resident` 的单窗关闭行为保留。

有限生命周期短测使用 `--resident --resident-smoke-test --react --page=data`：等待三个页面 ready，关闭中间窗，重载其余两窗并重建中间窗，关闭全部业务窗后保留控制器至少 500 ms，再重建三窗并统一退出。180 秒内未完成返回非零；监督器应另设进程超时。它不自动操作菜单或发送快捷键，也不证明页面中真实任务互不干扰、用户输入、IME、Explorer 重启、长期资源或 Win7 兼容通过。

驻留模式的 NDJSON 事件默认写到系统临时目录、按进程和运行标识生成新文件；验收可传 `--resident-log` 指向仓库外新的绝对文件路径，保存槽位、代际、ready、关闭、各服务和最终 Runtime 退出结果。禁止覆盖既有证据。不要与单窗 smoke/self-test、benchmark 或时序实验参数组合。桌面验收必须与其他 SDK 窗口和 QEMU 串行，x86 使用对应 x86 输出。公共服务 API 见 [SDK 文档](../../docs/api.md)。
