# 跨平台 Runtime

本页说明真正可编译、可链接的 **无窗口 Runtime**。同一开发 PR 另提供
[统一系统 WebView 桌面入口](unified-desktop.md)，包含三个真实后端源码和一份
通用示例；它有独立依赖与未完成的 GUI 验收，不能与本页 Runtime 实测混淆。
原有 SOUI/Miniblink 桌面路径仍是 Windows 专用。冻结的 0.5.0 发布包不含本功能，不以相同版本替换任何已发布包；
正式交付前须协调后续开发版本、源码清单和目标系统验收。

## 目标矩阵与证据

| 目标 OS | 架构 | Runtime 源码/构建策略 | 原有桌面 Host | 本 PR 执行证据 |
|---|---|---|---|---|
| Windows | x86、x64 | 支持，C++17/MSVC | 现有支持范围，仍需原验收 | 本轮未在 Windows 编译/运行 |
| Windows | ARM32、ARM64 | 支持，须匹配的 MSVC/SDK 目标组件 | 不支持，缺目标宿主/依赖 | 未编译/运行 |
| Linux | x86、ARM32、ARM64 | 支持，须目标编译器、sysroot、C++17 标准库 | 不支持，未实现窗口后端 | 未编译/运行 |
| Linux | x64 | 支持，GCC/Clang C++17 | 不支持，未实现窗口后端 | 本地编译/测试，详见 PR 命令和结果 |
| macOS | x64、ARM64 | 支持，Apple Clang/C++17 和 Security.framework | 不支持，未实现窗口后端 | 未编译/运行 |
| macOS | universal2 | 同一次构建包含 x86_64 和 arm64 | 不支持 | 未编译/运行，不能只跑一个 slice 就判双架构通过 |
| macOS | x86、ARM32 | 明确拒绝，现代 macOS 不支持这些 32 位应用目标 | 不支持 | 仅拒绝策略单测 |
| 其他 | ARM64EC、未知 OS/架构 | 明确拒绝，不猜测 x64 | 不支持 | 仅拒绝策略单测 |

“支持”表示代码和构建路径，不等于目标机验收。矩阵测试中的合成 Windows、
ARM 和 Apple 参数只验证选择/拒绝规则，不能替代目标工具链编译或运行。
ARM32 包含不同 ISA/浮点 ABI，必须通过工具链和 sysroot 明确指定；不提供
一个可覆盖所有 ARM 设备的通用二进制。大小端、ABI、libc、最低系统和
C++ 标准库必须与消费方匹配，静态 C++ 库没有跨编译器稳定 ABI。
libstdc++ 必须使用现代 C++11 ABI；旧 `_GLIBCXX_USE_CXX11_ABI=0` 的
copy-on-write 字符串不满足现有 TaskStore 的无抛出交换契约，配置时明确拒绝。

## 实际实现范围

`Soui::Runtime` 包含既有 result、event_bus、task_store（含配额/观察）、
webview_session、permission_store、action_contract、bridge_transport、
task_service、GUI 命令协议协调器、directory_scanner 和 FileinsightService。
实现复用现有源文件、授权、取消、背压和关闭契约。

- GUI 协调器只执行协议和授权逻辑，不创建窗口、不连接系统辅助功能。
- 不包含 `json_bridge` Windows 桌面 facade、`webview_host`、SOUI/Miniblink、
  WebView2、Windows UIA、命名管道 Agent、剪贴板/托盘/菜单/快捷键。
- 安装目录仅提供已实现的公共头文件；不可用组件在配置期报错。
- Runtime 的 `native_connection_binding` 不是系统身份认证；应用仍须验证
  自己的可信通道和对端，才能调用本地授权 API。
- Windows 桌面 `Soui::Sdk` 与 `Soui::Runtime` 是替代档位，不要在同一程序
  同时链接两套 Runtime 实现。Web TypeScript 包仍遵循原 Chrome 49 目标。

## 本机构建、测试与安装

前提为 CMake 3.24+、C/C++17 编译器及平台开发 SDK；测试矩阵另需 Node.js
18+。不自动安装工具，不下载 renderer，不启用远程 CI。

```sh
cmake --preset portable
cmake --build --preset portable
ctest --preset portable
cmake --install out/portable --config Release
```

默认生成器由 CMake 选择；可在首次配置时指定 `-G Ninja`（需自行准备 Ninja）。
也可以不使用 preset：

```sh
cmake -S . -B out/runtime -DSOUI_BUILD_PORTABLE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build out/runtime --config Release
ctest --test-dir out/runtime -C Release --output-on-failure
```

必须使用独立构建目录，不能把已有 Windows 桌面缓存直接改为 portable。
portable 模式不静默忽略显式开启的模板、Demo、WebView2、UIA 或 vendor 测试。
原 Windows `tools/Build.ps1` / `tools/Test.ps1` 继续服务原桌面链路。
`cmake -S sdk -B ... -DSOUI_BUILD_PORTABLE=ON` 支持仅构建/安装库，不注册根目录测试。

在完成上述 preset 构建及 `cmake --install out/portable --config Release` 后，
可运行独立消费者（CTest 也会自动在全新的隔离安装目录验证一次）：

```sh
cmake -S tests/portable/consumer -B out/runtime-consumer \
  -DCMAKE_PREFIX_PATH="$PWD/out/portable-install"
cmake --build out/runtime-consumer --config Release
ctest --test-dir out/runtime-consumer -C Release --output-on-failure
```

PowerShell 中可使用 `(Resolve-Path out/portable-install).Path` 替代上述 `$PWD`
拼接，并将多行命令写在一行。业务项目使用
`find_package(SouiRuntime CONFIG REQUIRED COMPONENTS Runtime)` 和
`target_link_libraries(app PRIVATE Soui::Runtime)`。包会核对目标 OS/架构，
不会将 Linux x64 静态库误当成 Windows 或 ARM 包。

## 架构选择与交叉编译

### Windows / MSVC

在具有所需目标编译器和 Windows SDK 组件的机器上，分别创建目录：

```sh
cmake -S . -B out/runtime-win32 -A Win32 -DSOUI_BUILD_PORTABLE=ON
cmake -S . -B out/runtime-x64 -A x64 -DSOUI_BUILD_PORTABLE=ON
cmake -S . -B out/runtime-arm32 -A ARM -DSOUI_BUILD_PORTABLE=ON
cmake -S . -B out/runtime-arm64 -A ARM64 -DSOUI_BUILD_PORTABLE=ON
```

`-A` 仅用于支持该参数的 Visual Studio 生成器。ARM32 工具链组件在部分现代
发行版中不再提供；若未安装则失败，不能用 ARM64EC 或 x64 模拟包代替。
保留既有动态 Release CRT；调用方须匹配。交叉编译成功后仍须在匹配目标机
执行 CTest，不能在 x64 主机直接把 ARM 程序当成本机测试。

### Linux

提供 GNU 工具链示例，均要求调用方先准备对应工具链和目标库：

```sh
cmake -S . -B out/runtime-x86 -DSOUI_BUILD_PORTABLE=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/Linux-x86.cmake
cmake -S . -B out/runtime-arm32 -DSOUI_BUILD_PORTABLE=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/Linux-arm32.cmake
cmake -S . -B out/runtime-arm64 -DSOUI_BUILD_PORTABLE=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/Linux-arm64.cmake
```

x86 示例要求 GNU-compatible 编译器的 `-m32` 和 multilib；
ARM 示例使用 `arm-linux-gnueabihf-*` / `aarch64-linux-gnu-*`。
自定义目标通过自有 CMake toolchain 明确编译器、sysroot 和目标 ABI。
64 位原子在目标链接器不能直接提供时探测 `libatomic`，缺失即失败。
交叉测试需目标机或调用方明确配置的 `CMAKE_CROSSCOMPILING_EMULATOR`；
本项目不自动下载/启动模拟器。

### macOS

```sh
cmake -S . -B out/runtime-mac-x64 -DSOUI_BUILD_PORTABLE=ON -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake -S . -B out/runtime-mac-arm64 -DSOUI_BUILD_PORTABLE=ON -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --preset portable-macos-universal2
cmake --build --preset portable-macos-universal2
```

universal2 明确要求两个 slice；目标最低系统通过
`CMAKE_OSX_DEPLOYMENT_TARGET` 与所用 Apple SDK 决定，必须单独验证。

## 依赖和失败边界

```sh
node tools/platform-info.cjs
node tools/platform-info.cjs --matrix
node tools/platform-info.cjs --probe-toolchain
node --test tests/portable/platform-matrix.test.cjs
```

报告区分平台策略、工具链探测和 Runtime 实测；工具链成功不自动改变
Runtime 验收状态。CMake 在目标编译器启用后核对实际预定义宏和指针宽度，
不把宿主 `process.arch` 当成交叉编译目标，也不以“8 字节指针”猜测 x64。

权限令牌和非 Windows GUI UUID 使用系统 CSPRNG：Windows BCrypt、
Linux getrandom、macOS SecRandomCopyBytes。Linux 要求内核 3.17+ 和提供
`sys/random.h/getrandom` 的 libc（glibc 2.25+）；熵池未初始化、系统调用
不支持或其他错误都使授权失败，没有弱随机降级。中断重试有上限。
原微秒 `steady_clock` 观察保持不变。

## 桌面适配与剩余工作

`Soui::Desktop` 已独立实现 GTK3/WebKitGTK、WKWebView、WebView2 受管理多窗口和
统一导航/Bridge、平面菜单/托盘及 SDK 开发 ZIP 入口。它没有把原有 SOUI 产品能力完整移植，也没有
完成全矩阵桌面验收。仍须推进：

1. 对已实现窗口/消息泵和关闭边界执行真实目标机验证，补齐输入法、DPI、多屏、
   拖放，并验证多窗口与独立会话。
2. 完成三个系统引擎的实际编译/交互矩阵，完善 root-scoped 资源服务与现代 Web
   资源兼容性，不以 fake-backend 或 VM 代替真实页面 Bridge 测试。
3. ARM 对应 SOUI/Miniblink 或替代后端的可追溯源码构建、二进制 ABI 与许可。
4. 对已实现目录选择/剪贴板/菜单/托盘补实机证据，移植统一快捷键、可访问性
   与本地 Agent 对端认证。
5. 平台签名/打包、安装升级回退、真实输入/窗口/长时间任务及全矩阵目标机验收。

这些缺口不会通过返回空成功或声称“全部架构已适配”来隐藏。
设计记录见 [ADR-0118](adr/0118-portable-runtime-core.md)。
