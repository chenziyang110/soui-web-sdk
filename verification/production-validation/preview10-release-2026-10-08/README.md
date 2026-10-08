# preview.10 发布验证

日期：2026-10-08。SDK 版本：`0.3.0-preview.10`。源码身份：`de5dbc77aa7aa27ad8bdae0ed079f98c6275fd152f5b34d0b3ec1641fe55735e`。固定 SDK manifest 身份：`8a967dbdb544e0d5295b0fabfb6ef75665e8a673fc4bbdba15814a1d3f6e5a00`。

`standard/` 保存四组构建、原生就绪/关闭和测试记录。两架构 Release 与 Debug 每组八项原生测试及 Starter、Workbench Vue、Workbench React 三个场景通过。标准入口另通过14项包检查、17项交付与发布工具回归，以及98项 Web/验收协议测试；未配置的真实 CDB 抓取检查跳过一项。独立 WebView2 证据与预加载边界测试13项通过。

`generated-project.json`、`fresh-project.json`、`relocation.json` 对应六次仓库外 x64 Release 独立构建与真实启动：新模板、preview.9 基线、升级至 preview.10、回退至 preview.9、再升级、复制后重新构建。升级夹具明确使用旧公共 API，业务17个文件的身份保持；不声称新模板能回退到旧 SDK 编译。外部 CMake 消费者在全新目录构建，指向本轮实际 SDK 安装，记录 EXE 和实际链接库散列。

`dependencies-x86.json` 与 `dependencies-x64.json` 保存源码重编译和六个新编 DLL 运行证据。底层源码与此前固定输入相同，源码构建在2026-10-03完成，本版实际运行检查在2026-10-08完成。原有预编译发行依赖未替换。

`summary.json` 汇总两种引擎的当前版本回归；`miniblink-*` 保存重载、销毁重建、多窗口与 Bridge 原始记录，`webview2-*` 保存重载与 Controller 重建原始记录。每场景10轮。WebView2 当前系统 Runtime 为154.0.4258.62，业务报告前验证动作契约、GUI 状态等待、非法参数拒绝、原生白名单与异步页面响应。

元数据和人类可读日志中的本机目录替换为占位符，文本日志去除尾空白。原始 `.ndjson`、`.jsonl` 按字节保留，散列与结果一致；路径替换不改变来源、载荷或事件散列。

失败过程保留：本目录修复前依赖审计及 Vue 指纹保护拒绝构建的日志；2026-10-03 Tailwind 构建链审计保留在相邻目录。恢复时更新 source-map-js 补丁和 Vue3.5.43，核对三处 CSS 缓存补丁后重新冻结源码、完成全部矩阵。审查发现的陈旧收据、旧 CMake 安装目录、失败尝试遗留绿色状态均已修复，发布回归覆盖旧源码/SDK身份和缺失阶段拒绝。

`preview10-packaged-archives.json` 记录五个固定 SDK/应用归档的实际解压验证。完整源码包另由暂存清单生成，发布资产附 SHA-256 与归档验证结果。

完整工程交付汇总通过；生产状态仍为 `releaseReady=false`。本轮不覆盖8小时浸泡、千轮资源收敛、Electron同负载性能、全目标实机、输入法/DPI/多屏/UIA、跨平台、外部Agent认证或完整二进制再分发审核。短循环不得替代这些门槛。
