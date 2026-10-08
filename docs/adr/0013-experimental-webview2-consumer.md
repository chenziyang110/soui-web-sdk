# ADR-0013：独立 WebView2 实验消费者

日期：2026-10-03。状态：实验实施，非正式后端支持承诺。

## 背景与决定

preview.9 已抽出宿主适配器，但只有 Miniblink 实现。R02 需要真实的现代渲染消费者，检验相同 C++ Bridge 和 Web 连接器能否跨后端工作。新增 `experiments/webview2-consumer/`，通过安装后的 `Soui::Sdk` 构建，默认 SDK、模板、Demo 构建均不携带此依赖。

WebView2 依赖 UI STA 消息泵完成异步初始化，不能在同步 `mount()` 中阻塞等待。实验应用先异步创建 Environment、Controller 并完成预加载脚本注册，再将已准备适配器交给通用宿主；`mount()` 注册并发起本地导航。文档就绪和业务就绪独立报告，初始化失败必须退出失败。

固定官方 NuGet `Microsoft.Web.WebView2` 版本与包散列，只在明确运行实验构建时恢复到 `out/`。使用系统安装的 Evergreen Runtime；不自动安装、不把运行时体积计作零、不改变既有 Windows/Chrome49 支持边界。实验先在当前 Windows 开发机验证。

## 对象与状态

| 对象 | 责任与失效条件 |
|---|---|
| UI STA / Controller | WebView2 COM 调用仅在所属 UI 线程；异步完成回调必须检查关闭状态 |
| 通用 Host | 只接收已准备的适配器；挂载与文档完成分离；支持幂等销毁 |
| Bridge 工作队列 | 有界请求和响应；业务不在 WebView2 回调阻塞；导航使旧代请求与响应失效 |
| 文档会话 | 限可信本地顶层页面，导航与关闭撤销旧凭据和任务；晚到结果不能送入新文档 |
| 实验窗口 | 保持适配器与业务对象生命周期；在正常消息循环之外完成最终清理 |

## 信任与兼容边界

只映射指定资源目录到固定 HTTPS 虚拟主机，核对消息来源及当前文档；阻止未允许的导航、子框架导航、弹窗、下载和权限请求。不开启宿主对象。原生能力只使用既有 Bridge 验证，网页不能授予自己手势；本实验不启用需要真实手势的桌面模块。

预加载脚本提供现有 `__souiNativePostMessage` Promise 传输与事件接线。使用结构化 JSON 回送，不将不可信消息拼接为脚本。普通错误、容量拒绝、超时、文档卸载必须可观察。安装后的 SDK 是本实验唯一 C++ 依赖入口，业务仅注册确定性的测试方法。

备选方案：直接替换默认内核会扩大兼容风险；将异步初始化伪装成同步 mount 成功会破坏宿主契约；先在公共 SDK 增加完整异步生命周期接口需要更广的迁移。先用独立消费者暴露问题，验证后再决定正式接口。

## 验证与恢复

验证 x86/x64 独立构建、真实 bootstrap→discover→业务调用→就绪、重载/关闭/重建、非法来源与参数拒绝、传输超时与晚到事件。记录源码、依赖包、安装库和执行载荷身份，保留失败证据。可选实验目录移除即可回退，对现有交付默认无影响。输入法、多屏、无障碍、离线分发、资源趋势、长稳和完整安全边界仍须独立验收。

参考：[线程与重入约束](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/threading-model)、[安全接入](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/security)、[固定 SDK 包](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4258.31)。
