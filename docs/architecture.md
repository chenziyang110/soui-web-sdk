# 架构与所有权

## 三层交付

SDK 提供宿主运行、独立引擎线程、交互协议、Bridge、会话、权限、任务与 Web 客户端。模板选择最小页面和业务注册入口。完整 Demo 展示媒体与扫描等业务，不参与 SDK 编译。

```mermaid
flowchart LR
  Page[React 业务页面] --> WebSDK[TypeScript SDK]
  WebSDK --> Host[本地文档与消息校验]
  Host --> Adapter[bridge_adapter 接口]
  Adapter --> Business[应用注册的 C++ 服务]
  Host --> Engine[Miniblink 引擎线程]
  Host --> Window[SOUI 原生窗口]
```

`sdk/CMakeLists.txt` 直接生成 `soui_sdk.lib`，不包含任何模板或 Demo 文件。应用链接 `Soui::Sdk`。已安装的 SDK 包通过 CMake `find_package` 引入；它不依赖 SDK 的原始源码位置。

preview.9 的原生 `tray_service` 属于应用级桌面服务，由应用的隐藏顶层窗口及其线程持有，寿命独立于任一 Host/文档会话。服务不启动 WebView、不另起线程，也不决定最后业务窗口关闭后的进程策略。应用先明确移除托盘并销毁服务，再销毁其所有者窗口；详细状态和恢复边界见 [ADR-0012](adr/0012-public-tray-service.md)。

preview.10 增加同线程的 `menu_service` 和 `global_shortcut`，不向 Web 注入权限。Workbench 的显式 `--resident` 控制器持有这三项服务；三个业务槽位各有独立 adapter、Host、窗口和会话，共享进程 Runtime。SOUI 业务窗明确使用普通窗口类型，关闭请求延迟到控制器处理；销毁单窗不会自动结束消息循环。全部业务窗关闭后仍驻留，可由托盘菜单或 Ctrl+Alt+W 恢复；显式退出按菜单、快捷键、托盘、业务窗、Runtime、控制器顺序清理。默认单窗方式保留。实现与验证边界见 [ADR-0013](adr/0013-public-menu-shortcut-and-residency.md)。

## 目录所有权

| 目录 | 维护责任 | 是否随 SDK 安装器更新 |
|---|---|---|
| `sdk/` | SDK 公共接口与实现 | 是 |
| `cmake/` | SDK 应用构建和部署规则 | 是 |
| `third_party/include,prebuilt,sources/zlib,licenses,manifests` | 固定的 SDK 依赖 | 是 |
| `tools/sdk,internal` 和 `tools/Build.ps1` | SDK 安装与构建工具 | 是 |
| 模板生成后的 `app,web,contracts,resources` | 产品业务团队 | 否 |
| `templates` | 模板维护者 | 不自动合并到已有项目 |
| `demos` | SDK 示例维护者 | 不复制到最小业务项目 |
| `third_party/sources/miniblink,soui` | 底层维护者 | 包含在完整源码包，避免每个新项目重复复制 |

## 扩展接口

`soui::sdk::bridge_adapter` 是应用与宿主的边界，宿主不依赖 Demo 的类型。简单应用使用 `json_bridge` 注册方法；复杂业务可以实现接口并复用 runtime 的授权和任务组件。`demos/workbench/integration` 展示完整异步任务实现。

UI、引擎与 Bridge worker 是不同线程。适配器必须比宿主存活更久；注册在启动前完成。运行期共享状态需要同步。关闭时先停止宿主，再释放适配器。导航切换撤销旧会话及其权限。

preview.3 开始由进程级 Runtime 统一持有 Miniblink 和引擎线程；每个 Host 独立持有 WebView、交互端点、Bridge worker 和会话。窗口销毁后仍可创建新窗口，最终应用退出另调用 `shutdown_miniblink_runtime()`。全局 JavaScript 绑定按执行上下文所属 WebView 路由到对应 adapter。设计、限制与验证要求见 [ADR-0003](adr/0003-process-runtime-ownership.md)。

模板和 Workbench 默认使用稳定的软件呈现路径。GPU compositor 仍可供内核专项实验，但当前关闭路径存在访问冲突，不能作为生产模板默认值；决策和复验边界见 [ADR-0005](adr/0005-stable-renderer-default.md)。

## 编译与 ABI

当前是 C++17 静态源码 SDK，使用 MSVC v143、动态 Release CRT（包括带调试符号的 Debug 配置）。公开 C++ 类不是跨编译器稳定的 DLL ABI；应用、SDK 和接口宏必须使用一致的构建设置。CMake 导出携带必要的头文件、编译特性和系统链接库。

性能实验宏和测试注入宏不属于标准发行构建契约。标准 SDK 的目标、模板和 Demo 使用相同的公开类型布局。
