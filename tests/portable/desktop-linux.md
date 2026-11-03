# Linux WebKitGTK 后端专项验收

`desktop_linux_backend.cpp` 使用真实 GTK3/WebKitGTK，不模拟网页执行，也不把缺少
显示服务视为通过。编译前需要 GTK `3.24+`、WebKitGTK API `4.1` 且引擎版本
`2.40+`。测试必须运行在允许 WebKit 原生沙箱和显示服务工作的 Linux 环境。

## 测试范围

- 创建窗口、UTF-8 标题、尺寸、显示/隐藏和焦点请求；
- 本地文档的 document-start preload、顶层标记与初始空白子 frame 隔离；
- 真实网页消息与原生 JavaScript 执行；
- 网络导航及新窗口拒绝；
- 原生剪贴板读写、目录选择窗口的取消；
- 工作线程投递只在 UI 线程执行，关窗取消尚未执行的投递；
- renderer `location.reload()` 被取消并请求新的原生 preload；
- reload 之前排队的消息不得送入新的文档代际；
- 幂等关闭和关闭后的 API 拒绝。
- 根原生窗口在进入事件循环前关闭后，子窗口仍能加载、收发消息并结束共享循环。

目录对话框由测试自身的 GTK 回调取消，不需要人工输入。三个阶段各有 20 秒看门狗；
超时、缺少显示服务、WebKit 进程错误均返回非零。

## CMake 验收入口

在正常图形会话的终端中从仓库根目录执行，使用发行版完整安装的开发和运行依赖：
若之前使用隔离 sysroot 编译，应先使用没有这些环境变量的终端和新的构建目录，
避免 CMake 缓存继续引用旧头库；下面 preset 假定 `out/desktop` 未缓存其他依赖。

```bash
cmake --preset desktop -DSOUI_DESKTOP_GUI_TESTS=ON
cmake --build --preset desktop
ctest --preset desktop -R '^sdk.desktop.linux-native$' --output-on-failure \
  --output-log "$PWD/out/desktop/linux-native.log" \
  --output-junit "$PWD/out/desktop/linux-native.xml"
```

CTest 总超时为 70 秒。保留日志、JUnit、当前源码提交和依赖版本；退出码非零时
不能记录为通过。运行前可用 `ctest --preset desktop -N -R '^sdk.desktop.linux-native$'`
确认恰好注册一个专项用例，但列出测试不等于运行。不得把只执行无 GUI 测试的
开发包构建结果当作原生验收。

## 单独编译

从仓库根目录执行：

```bash
mkdir -p out/linux-desktop-backend-test
c++ -std=c++17 -Wall -Wextra -Wpedantic \
  $(pkg-config --cflags gtk+-3.0 webkit2gtk-4.1) \
  -I sdk/include \
  tests/portable/desktop_linux_backend.cpp \
  sdk/src/desktop/backends/linux_webkitgtk.cpp \
  $(pkg-config --libs gtk+-3.0 webkit2gtk-4.1) -pthread \
  -o out/linux-desktop-backend-test/desktop_linux_backend
out/linux-desktop-backend-test/desktop_linux_backend
```

使用提取的 sysroot 时，系统依赖目录应作为编译器 system include，而不是项目
头文件。运行时还需要相应的动态库和 WebKit helper 路径；正式发行环境应使用
发行版正常安装的官方软件包。不得用关闭沙箱、关闭 Web 安全检查或修改系统安全
配置的方法让测试通过。

## 明确的平台边界

WebKitGTK 4.1 的
[script-message-received](https://webkitgtk.org/reference/webkit2gtk/stable/signal.UserContentManager.script-message-received.html)
没有发送方 frame/origin 字段。后端上报 `message_frame::unknown`，当前 WebView
URI 只表示导航上下文。公共层仍必须验证顶层 preload 闭包内的随机令牌，不能把
该 URI 当成消息来源证明。同源代码能够调用父文档公开接口，不构成独立的安全主体。

[NavigationAction](https://webkitgtk.org/reference/webkit2gtk/stable/struct.NavigationAction.html)
同样没有可靠的主 frame / 同文档标志。因此每次原生 `load` 只发放一次文档导航
许可；`location.reload()` 明确分类为 reload 时取消旧操作并请求公共层轮换令牌。
没有原生许可的其他完整导航，包括 `location.assign(currentURI)` 和有 URL 的
子 frame 导航，保守拒绝。不会凭借 frame 名称或 URL 中的 `#` 放宽许可。
应用使用公共 `reload()` / `navigate()` 获得跨平台的确定行为。

GTK clipboard API 是异步的，本同步外观使用仍处理 UI 事件的嵌套主循环，最多等待
2 秒，文本上限 1 MiB；晚到回调只持有独立请求状态。目录选择允许用户持续交互，
关闭主窗口会销毁对话框并退出等待。焦点最终受桌面窗口管理器策略约束。

## 本轮事实

2026-10-20，Linux x86_64 的官方 Debian GTK `3.24.49` 与 WebKitGTK `2.52.6`
头文件及库已实际完成后端和专项测试的严格编译、链接：
`-std=c++17 -Wall -Wextra -Wpedantic -Werror`，依赖目录使用 system include。

最初执行测试二进制时没有可连接的 X11 / Wayland 服务，测试返回
`E_DISPLAY`、退出码 `2`。后续在受支持的既有云图形桌面启动样例，但系统前缀
缺少 `WebKitNetworkProcess`，尚未加载页面；详见
[实际记录](../../docs/acceptance/unified-desktop-2026-10-20.md)。
真实窗口、网页 IPC、目录对话框、剪贴板、reload 与
关闭竞态均尚未取得运行通过证据；编译通过不代表上述 GUI 行为通过。
