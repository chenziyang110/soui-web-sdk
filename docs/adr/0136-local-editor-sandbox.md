# ADR 0136：本地可信编辑器的隔离子框架策略

状态：本地开发候选；2026-10-29。适用于 0.6.0-preview.9 源码接入，不改变发布或生产验收结论。

## 背景

Scratch GUI 15.3.0 的造型编辑器使用隐藏的 Paper.js iframe 沙盒，源码将其设置为 `sandbox="allow-scripts"`。其 UMD 还包含旧浏览器 resize-sensor 的 object 后备实现及在线教程播放器代码。SDK 默认的静态启发式检查拒绝这些表达式；WebView2 默认也拒绝所有框架导航。默认合同继续适用于一般应用。

## 决定

增加原生配置 `local_resource_policy::isolated_local_editor`，默认仍为 `strict`。只有明确声明支持此模式的后端可接收它，当前仅 WebView2；MiniBlink 以及未声明支持的自定义后端在挂载前拒绝。

原生应用方选择该模式即声明资源目录为自己审核、封装并信任的应用代码。SDK 保留目录、index.html 和文件可读性检查，跳过源文件中框架及远程 URL 表达式的静态启发式扫描。该选择不接受网页、Bridge 参数或资源文件自行开启。

WebView2 从该次 mount 配置保存模式。仅在此模式下允许 `about:blank` 与 `about:srcdoc` 子框架导航；其他子框架导航继续取消。顶层导航仍固定至 SDK 的受控入口，所有来源的外部资源请求继续返回拒绝；原生 preload 仅在顶层受控入口注入功能，且不注册框架 WebMessage 处理器。

SDK 不检查 iframe 的 sandbox 属性，不证明任意 srcdoc 内容安全，也不将此模式当作加载不可信 HTML 的能力。可信应用代码负责对子框架使用适当的隔离属性；Scratch 的已审核构建采用不含 allow-same-origin 的 allow-scripts 沙盒，因此该子文档为 opaque origin。保留完整资源文件及源码来源以便审查。

## 兼容性与回退

既有调用默认行为不变。host_config 和适配器虚表新增成员，需要同时重编 SDK 与使用方，不能混用旧二进制。回退方式为移除原生 opt-in，恢复 strict，使用不依赖框架的页面。WebView2 默认的远程请求、权限、下载、顶层导航和 Bridge 会话边界保持原规则。

## 验证

定向契约验证涵盖 strict 拒绝框架、未支持后端拒绝 opt-in、支持后端只在显式 opt-in 后接收可信包、无 index 或不存在目录仍拒绝、未知枚举值拒绝。实际编辑器验证还须覆盖框架启动、代码与舞台、造型、离线资源、顶层 Bridge 与关闭；源码/编译通过不替代这些运行证据。
