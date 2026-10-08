# WebView2 独立 SDK 消费者

0.4 起此工程链接安装后的公共 `Soui::WebView2`，验证 Host、`json_bridge` 和 `connectSouiNative` 的真实生命周期。适配器实现已移入 SDK，可进入选择现代档的生成项目；此测试应用本身保持独立。构建记录同时核对基础 SDK、现代组件和 Loader 身份；历史 preview.9/10 实验证据保持原身份。

## 环境与启动

需要项目已有的 Windows / VS2022 C++ 工具链，以及单独安装的 WebView2 Runtime。构建只恢复固定版本的 WebView2 开发包，不安装系统运行时。

从交付根目录执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Build.ps1 -Architecture x64
```

构建会输出一个位于 `out/webview2/` 的独立构建目录。使用输出的实际目录运行重载和销毁重建探针：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File experiments/webview2-consumer/Test.ps1 -BuildDirectory out/webview2/<构建ID> -Scenario all -Cycles 10
```

32位构建改用 `-Architecture x86`。`-SkipSdkBuild` 只在当前 SDK-only 构建证据仍有效时复用本地 SDK。每次测试保留新的运行记录，包括原始退出码、超时状态和事件日志。缺失运行时、初始化失败、页面未就绪、缺失步骤均不能通过。

恢复依赖可单独执行 `experiments/webview2-consumer/Restore.ps1`。`dependency.json` 固定官方包来源与 SHA-256，恢复时核对压缩包及实际使用的头文件、两架构 Loader、授权文本。运行时版本由实际环境报告。缓存及编译产物只保存在 `out/`；复验前不要修改这些产物。

## 验证的业务

页面使用 ES Modules，并核对 CSS Grid 和 TextEncoder。每次新文档通过公共连接器 bootstrap，分页发现实际方法，执行16个原生往返，核对业务错误传播与后续恢复，验证120ms原生延时期间网页计时器继续推进。preview.10 还通过显式注册的 GUI 按钮动作调用原生问候契约，等待结果状态，并核对非法输入拒绝和原生白名单拒绝，全部通过后才提交业务就绪报告。

WebView2 Environment / Controller / 预加载脚本先异步准备，正常 STA 消息循环保持运行；通用 Host 再挂载已准备的适配器。C++ Bridge 请求进入有界后台队列，控制器调用仍留在所属 UI 线程。导航代标识防止旧文档响应流入新文档。

实验传输最多保留128个请求、128个待回送记录，单条Bridge内容上限64 KiB，回送内容总量上限8 MiB；页面等待有超时。它不提供现有 Miniblink Host 的独立控制槽位，也尚未验收完整公共长任务事件链。关闭会先停止接收，等待本实验的有界处理器完成；异步等待期间返回 `E_BUSY` 并由消息循环重试。

## 当前边界

仅加载固定 HTTPS 虚拟主机下的本地顶层文档；限制导航、框架、弹窗、权限和下载。不开放宿主对象，也不把页面标志当作真实用户手势。本实验不启用剪贴板、目录选择等手势能力，不提供任意网页浏览器。

静态 Loader 和系统 Evergreen Runtime 是两个不同依赖。EXE 大小不代表完整安装成本；Runtime 安装大小、浏览器进程树内存、后台回收和离线部署仍需独立测量。短启动与循环探针不证明8小时稳定性、生产安全、跨平台支持、输入法/DPI/多屏/无障碍或 Electron 性能对齐。

关于正式适配器接口、兼容与恢复，见 [ADR-0013](../../docs/adr/0013-experimental-webview2-consumer.md)。[2026-10-03实际验证](../../docs/acceptance/webview2-experiment.md)：x86/x64重载、Controller销毁重建各10轮，共40轮通过；同轮Miniblink四场景共80轮回归通过。失败记录与未覆盖项一并保留，`releaseReady` 保持 false。

## 第三方资料

- [Microsoft.Web.WebView2 1.0.4258.31](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31)：构建时恢复官方开发包；保留其 LICENSE.txt 与 NOTICE.txt。
- [线程模型](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)：UI STA、消息泵与异步回调约束。
- [安全接入](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)：验证消息来源、限制文档与宿主能力。

此目录不重新分发 WebView2 Runtime；下游若分发运行时，应另行遵循对应分发说明。
