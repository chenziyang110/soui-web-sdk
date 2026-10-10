# 共享业务参考工作台（W6 / 05-06）

这是独立、可选的业务示例。三个页面使用相同固定 10,000 行模型、公开 actiongen 表单和同一真实 C++ json_bridge 任务服务。已有 Modern starter 和 Workbench 未改。候选未构建、未运行；全部发布、性能、资源、操作系统和 IME 资格保持 false。

| 页面 | 已冻结依赖 | 原生目标 |
|---|---|---|
| Modern React | React 19.3.0、Vite 8.3.3，语义 HTML / SVG | SouiReferenceModern / WebView2 |
| Compatibility React | React 18.3.1、Vite 6.4.3、PrimeReact 10.9.9 Button | SouiReferenceCompatReact / Miniblink |
| Compatibility Vue | Vue 3.5.43、Vite 6.4.3、Element Plus 2.14.7 Button | SouiReferenceCompatVue / Miniblink |

两份 package.json / package-lock.json 是当前 starter / Workbench 的原字节副本，保留原包名、包版本和锁。它们不另定义 SDK 版本，也不升级依赖。兼容页使用本目录 web-compat/src/compat-runtime.ts 的 guarded runtime、tools/vue-miniblink-compat.ts 的精确 Vue CSS-cache 修补和 Chrome 49 classic 构建边界；仅这几个控件进入示例。不能据此声称所有最新 JS/CSS 或这些组件库的全部功能都适用旧内核。现代页面不新增第二套大组件库。

原生 worker 重建同一固定样本，任务输入只有 fixtureId/category/durationMs。执行上限 60 秒、固定 10,000 行；duration 是让界面和任务并行的可取消最小演示时段，不是吞吐或 CPU 性能指标。TaskService 原有固定 worker 池/有界队列仍由 SDK 持有。页面排序、过滤、选择最多 100 行，显示最多 80 行 DOM；表单有 UTF-8 上限。任务结果只从公开查询读取，取消请求不等于终态。

本地存储最多 8 KiB，仅包含草稿、筛选、选择、主题和历史任务引用。页面明确显示 empty/restored/rejected/storage-unavailable；刷新后的旧 taskId/operationId 标记 previous-document-unknown，不查询、不重放、不持久化 bearer/scope/token。窗口初次打开用同一公开 Bridge 建立真实新文档；导航的实际授权撤销仍由 SDK Host 负责。表单不写文件。

SouiGui 注册 12 个明确业务节点；按钮与程序化自检进入相同动作。状态只提供长度、数量和任务状态，不把名称/备注值加入 GUI snapshot。GUI 入口不制造 native user gesture、UIA 或 Agent 权限。公开 bridge-owned stopped endpoint 工厂虽已采用，本示例不启动端点、不额外授予 Agent scope，也不把 local-only family 暴露到页面。

## Root 的最小构建入口

所有下面命令只能由 Root 使用既有严格 Owned/桌面租约运行；不是脚本授权或实际通过记录。先在完整 current source delivery 下采用本新增目录，或做 Root 管理的有界源映射。公共 API 相对导入依赖相邻 sdk；运行时与 Vue 转换已在本目录中，resize-observer-polyfill 从对应 web-compat 原锁安装解析；不能只把单个 candidate 子目录当作已完成安装包。

1. 生成并检查 typed actions（此 lane 没有写假生成文件）：

    node tools/sdk/actiongen.cjs --source demos/reference-workbench/contracts/actions.json --root demos/reference-workbench
    node tools/sdk/actiongen.cjs --source demos/reference-workbench/contracts/actions.json --root demos/reference-workbench --check
    node --test --test-reporter=tap demos/reference-workbench/tests/model.test.mjs demos/reference-workbench/tests/task-controller.test.mjs demos/reference-workbench/tests/initialization.test.mjs

生成物由原 actiongen 拥有：app/generated/actions.hpp、web/src/generated/actions.ts、contracts/generated/tools.json、docs/generated/actions.md 及 ownership manifest。CMake 保留原 --check，不宽松接受缺失/手写输出。

2. 各 Web 目录使用对应原锁 npm ci --ignore-scripts，不要用旧 node_modules。原继承 package scripts 指向旧示例工具，故本示例显式调用本地已锁工具：

    cd demos/reference-workbench/web-modern
    node node_modules/typescript/bin/tsc --noEmit
    node node_modules/vite/bin/vite.js build
    cd ../web-compat
    node node_modules/typescript/bin/tsc --noEmit
    node node_modules/vite/bin/vite.js build --mode react
    node ../tools/classic-entry.mjs dist-react
    node node_modules/vite/bin/vite.js build --mode vue
    node ../tools/classic-entry.mjs dist-vue

Vue SFC 的实际编译由已锁 Vue/Vite 插件执行；TS 命令不宣称完整 SFC 模板类型资格。单份 common/fixture.json 同时被页面导入并按原字节输出为每个 dist/reference-fixture.json。构建后须逐字节验证该文件与原 fixture，检查 actual Vite manifest/classic graph，无网络 fallback。

3. 用当前完整 source SDK 构建安装产物或 Root 当前正常 SDK target，再独立配置。示例支持 exact installed find_package(SouiSdk 0.5.0)；安装 SDK 不带 renderer runtime、Web 依赖或原 Workbench 源，不能把包发现成功误记为完整示例分发资格。Modern 开启前 SDK 必须包含 WebView2，可用 Runtime 仍需实际探测。两架构各用 fresh 短输出目录：

    cmake -S demos/reference-workbench -B out/w6/x64 -G "Visual Studio 17 2022" -A "x64,version=10.0.19041.0" -T "v143,host=x64,version=14.44.35207" -DCMAKE_PREFIX_PATH=<current-installed-sdk> -DSOUI_REFERENCE_DISTRIBUTION_ROOT=<current-complete-delivery> -DSOUI_REFERENCE_MINIBLINK=<current-full-source-x64-dll> -DSOUI_REFERENCE_MODERN=ON
    cmake --build out/w6/x64 --config Release --parallel 2
    ctest --test-dir out/w6/x64 -C Release --output-on-failure

Win32 必须 -A "Win32,version=10.0.19041.0" 及对应当前 x86 kernel/SDK。Root wrappers 清 CL/_CL_/LINK/_LINK_/NODE_OPTIONS、UseEnv=false、禁 Directory.Build /用户 props 与共享旧 obj；配置后实际检查 SDK/toolset/cache/PE 及完整新输出。CMake 只复制显式 kernel 路径、当前 CRT 输入和 Web/legal 资源，不回退旧 DLL。SDK 的正常源目标必须重编，不能借之前 factory/UIA/V7 不同源码的旧库获得资格。

## 实际业务验证与边界

38 个纯测试仅验证边界算法、任务观测控制及关闭文档时的实际初始化序列（mock client/timer，不冒称 native）。包括取消 ACK/拒绝和订阅拒绝晚于后续任务的负例，starting 观察者同步关闭后零原生派发，以及连接、发现、描述和 probe 等待期间关闭后的请求停止；4 个 CTest 通过真实公开 Bridge / TaskService 覆盖 fixed-summary、cancel、invalid-form、revoked-document，成功回执在真实 Bridge 析构 join 之后。它们不创建 GUI，不借私有服务或手工清 counters。

各应用 --smoke-test 只等待真实 application_ready 后完成 Host destroy；兼容程序随后调用独立 RuntimeShutdown。失败/超时保持实际非零或 Owned 强制闭合记录。此模式不代表业务场景通过。正常启动后“运行相同业务流程”会通过同一 GUI 动作执行中文表单确认、任务运行时 10k 排序/滚动/过滤/选择、主题、对话框、焦点、实际成功结果、第二任务取消终态和非法选择拒绝。页面输出实际步骤明细，仍保留 releaseReady=false。Root 应保存三个真实实例的步骤输出、源码/载荷/原始 Owned identity 与自然 0/0/no-force 闭合；页面自检不能升级外部 Agent、UIA 或输入资格。

重载单独执行：自检完成后保存界面并刷新，等待实际新文档/application-ready；确认草稿/筛选/主题/选择恢复结果及 previous-document-unknown，手动触发的新任务使用新 scope。尚未实施自动跨文档/真实键盘回执，不能把旧 ID 或静态 DOM 当作恢复证明。

真实中文 IME、Tab/Shift+Tab/Enter/Escape、焦点可见性、屏幕阅读器、全部控件能力、5×60 秒实际桌面呈现/输入、Modern 全进程树资源、长期/Win7/PE、安装重定位示例及发布许可核验仍待 Root 各自运行。普通浏览器可查看静态模型，原生缺失会显式禁用业务；没有 C++ 成功 mock。
