# 统一桌面入口本轮验证：2026-10-20

本记录只覆盖 PR #103 新增的未发布 `Soui::Runtime` / `Soui::Desktop` 路径，
不修改 `verification/acceptance.json`，不授予生产或全架构通过结论。

## 已实际执行

环境：Debian 13 x64、GCC 14.2、C++17、GTK 3.24.49、WebKitGTK 2.52.6、
Node 24.19。依赖来自官方 Debian 包，隔离解包供编译链接，未执行安装脚本，
未提交依赖二进制。构建工具与依赖路径只用于本地测试，不写入源码配置。

| 检查 | 配置与命令 | 结果 |
|---|---|---|
| 三层源码及真实 Linux 后端 | `cmake --preset desktop`，`cmake --build --preset desktop`，Release | 编译、链接通过；包含 Runtime、Desktop、同源码示例和原生 Linux 集成测试可执行文件 |
| 当前全部默认本地回归 | `ctest --preset desktop --output-on-failure` | 7/7 通过；其中 4 项 portable、2 项 Desktop 无窗口契约、1 项开发包工具回归 |
| 共用应用/协议边界 | `soui_desktop_contract` | 255 项检查、0 失败；fake backend，不调用原生窗口/系统能力 |
| 实际 preload 脚本 | `SOUI_DESKTOP_CONTRACT_EXECUTABLE=out/desktop/tests/desktop/soui_desktop_contract node --test tests/desktop/preload.test.cjs` | 10/10 通过；从实际编译代码输出脚本，在 Node VM 运行，不是真实浏览器 |
| 平台矩阵与拒绝配置 | `node --test tests/portable/platform-matrix.test.cjs`（使用实际 CMake） | 28/28 通过；合成架构策略不代表目标交叉编译 |
| 安装 | `cmake --install out/desktop --config Release` | Runtime / Desktop 安装包生成成功 |
| 独立消费者 | `cmake -S examples/unified-desktop -B out/desktop-final-consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/out/desktop-install"`，随后 build | 仅使用安装后的公共头文件和目标，编译、链接通过 |
| 前一轮独立 Debug 检查（非本次多窗口增量） | Desktop 关闭和开启两种配置，各运行 contract / preload | 两种配置均 2/2 通过；Desktop 开启时原生 Linux 集成目标编译链接通过，未执行 GUI |
| 前一轮 Linux 后端严格编译（非本次多窗口增量） | GCC `-std=c++17 -Wall -Wextra -Wpedantic -Werror` 配合实际 GTK/WebKit 库 | 后端及原生测试编译、链接通过 |
| 开发包入口 | `node tools/package-desktop.cjs --build-dir out/desktop --output out/desktop-package-managed` | build → 7 套 CTest → install → ZIP 实际完成；另核验 ZIP CRC、归档 SHA-256、34 个载荷文件字节数及 SHA-256 |
| 本轮独立消费者 | 使用上述开发包 `sdk` 作为唯一 `CMAKE_PREFIX_PATH`，独立配置、构建同源码多窗口示例 | 编译、链接通过，未运行 GUI |
| 地址/未定义行为检查 | GCC ASan+UBSan 重编公共契约，`ASAN_OPTIONS=detect_leaks=0` | 255 项通过；完整 LeakSanitizer 因环境 ptrace 不兼容报 fatal，不能宣称泄漏检查通过 |
| 补丁检查 | `git diff --check` | 通过 |

本次增量新增：窗口间 token/请求序号隔离、同 URI 不同窗口、根窗口先关闭、最后一窗退出、显式整组退出、32 窗口上限、跨窗模态回调串行化、菜单命令归属与输入限制。Linux 原生集成可执行文件已重新编译，仍未运行。Windows/macOS 新增菜单与托盘只有源码审查，尚无目标编译证据。

共用回归包含：URI 根边界、错误来源/frame/token、重复 ID、同 URI 原生重载、
旧代际消息与响应、回调内导航/关闭、模态消息重入、有界 dispatch、非法响应、
UTF-8 请求边界、超时/pagehide Promise 拒绝及 `__proto__` JSON 语义。

## 明确未通过验收

- Linux 真实窗口和 JavaScript/Bridge 集成：早期 shell 环境不能创建 Xvfb/D-Bus
  所需 Unix socket，提升入口也未能初始化。后续使用受支持的既有云图形桌面，经文件管理器确认同一源码目录可访问，并在该会话启动样例；不再把“没有显示服务”视为唯一阻塞。实际启动因系统前缀缺少 `WebKitNetworkProcess` 辅助进程而中止，尚未加载页面。隔离开发库仅能支撑编译；正式发行库没有采用临时辅助程序路径覆盖。当前仍需正确安装的系统 WebKitGTK 运行依赖。未绕过权限、修改第三方二进制或关闭 WebKit sandbox。
- Windows x86/x64/ARM64：没有 Windows SDK/MSVC 目标环境，后端只完成实现与
  源码/API 审查，未编译、链接或运行。原 Windows SOUI/Miniblink 默认路径未在
  本环境重建。
- macOS x64/ARM64/universal2：没有 Apple SDK，未编译、链接或运行。
- Linux ARM64/x86/ARM32：未交叉编译或实机运行。32 位目标还依赖发行版是否提供
  所需 WebKitGTK 开发/运行包及匹配 ABI。
- 未执行真实 DPI、IME、焦点、多屏、长期内存、重载稳定性、系统对话框和权限交互，
  没有 Electron 性能或逐像素对比结论。

原生目标机下一步按 [统一桌面说明](../unified-desktop.md)、
[Linux 用例](../../tests/portable/desktop-linux.md) 与
[macOS 检查单](../../tests/desktop/macos-wkwebview.md) 执行。Windows 先使用匹配
架构的官方 WebView2 SDK/当前受支持 Runtime 构建同一示例，再逐项执行窗口、
reload、双向 Bridge、系统能力、关闭与晚回调检查。真实引擎导航 ticket 的事件
顺序必须在各目标机确认，不能由 fake backend 的通过结论替代。

本仓库按既有约定不新增远程 CI。PR 保持 Draft，等待维护者审查及目标机验收。

## 生命周期及打包稳健性增量

本轮最终代码重新编译后通过全部七套 CTest：公共契约284项、preload10项、打包工具11项，以及portable四套检查。新增生命周期用例先对旧HEAD复现12个断言失败，修复后通过。独立安装包再次完成真实 build、CTest注册核对/执行、install和ZIP；仅引用该包的示例消费者编译链接通过。ZIP CRC、归档SHA-256及34个载荷文件哈希已重新核对。

新增Linux原生阶段覆盖“根窗口先关闭、子窗口仍加载/交换消息、最后结束根循环”，已用实际GTK/WebKit库严格编译链接。它未实际运行，不计入七套默认无窗口测试。Windows旧菜单选择的生成代际修复仍只有源码审查，未目标编译。上述GUI运行依赖阻塞尚未解决；本轮没有安装系统包。

## 显式本地资产读取增量

Linux x64 / GCC 14.2 / Release 完整构建（含真实 GTK/WebKit 后端、原生 fixture
和示例）通过；最终八套 CTest 全部通过。新增生产文件读取器使用真实临时文件，
957 项检查、0 失败、symlink 分支无跳过；公共 Bridge 契约303项、preload VM
16项、打包工具11项均通过。新增用例覆盖大小、MIME、原始字节、路径、特殊文件、
根边界、启用/禁用及窗口/来源/导航隔离。JS 孤立 UTF-16 代理字符导致超时的问题
在独立审查中发现，修复为发送前拒绝；有效 emoji 保留，最终 VM 回归通过。

改动的五个 C++ 翻译单元独立使用 `-Wall -Wextra -Wpedantic -Werror` 编译通过。
尝试全仓 `-Werror` 时，既有 Runtime 的缺字段初始化和符号比较警告导致失败；
未为此修改无关服务或放宽源码检查，完整构建按项目原有警告策略运行。
文件读取器 ASan/UBSan 通过，运行参数为 `ASAN_OPTIONS=detect_leaks=0`；
LeakSanitizer 受 ptrace 环境限制不可用，不能据此宣称无泄漏。

真实开发包入口重新完成构建、八项测试注册核对/执行、安装与 ZIP，仅引用安装包
的独立示例消费者编译链接通过。34个文件的字节数/SHA-256、ZIP CRC 和归档散列
核对通过。此记录不代表正式发布，仍保持 `releaseReady=false`。

未执行新的真实 GUI 或 Windows/macOS/ARM 目标运行。此前获得系统依赖安装许可
后，受支持的提升入口在 apt 启动前因只读文件系统初始化错误失败，未安装依赖；
未通过其他入口绕过限制。新资产 API 不改变这一验收边界，也不证明原生
ES module、fetch 或跨引擎 CORS 已得到统一支持。

## 异步资产读取增量

Linux x64 / GCC 14.2 / Release 最终重新构建、九套 CTest 全通过。生产工作线程
用确定性门闩执行282项检查，覆盖2线程/16作业上限、UI结果持有额度、排队与
运行中取消、读取未结束时销毁立即返回、异常恢复和取消/完成竞争；同一最终
测试连续执行100次通过。公共 Bridge320项检查，真实文件读取957项检查，
preload16项和打包11项均通过，刷新/关闭后旧结果不交付。

公共契约与改动实现以 C++17 / `-Wall -Wextra -Wpedantic -Werror` 编译通过。
工作线程测试 ASan/UBSan 通过，仍为 `detect_leaks=0`；LeakSanitizer 的 ptrace
限制未解除。进程固定工作池具有刻意的进程期生命周期，不能宣称退出前已回收。

真实开发包入口再次完成 build、九项测试注册核对/执行、install和ZIP；最终
安装包的独立示例消费者编译链接通过。34个载荷文件字节数/SHA-256及ZIP
CRC/归档散列核对通过。原生 GTK/WebKit 后端与 fixture 已编译链接，但未运行。
没有重试系统安装、修改安全配置或使用远程CI；Windows/macOS/ARM和真实GUI
验收缺口保持不变，未扩大正式发布或性能声明。

## 本地 HTTP 资源源代码增量（07:12 UTC）

按本轮“以实现为主，少量检查，后续统一回归”范围执行：

- Linux 隔离的真实 GTK/WebKitGTK 头库下构建 `soui_desktop`、
  `soui_desktop_example`、`soui_desktop_contract` 成功。
- 公共/fake-backend 契约 333 项通过（含新 loopback URI/来源边界）；
  app.js、feature.mjs、module-label.mjs 的 Node 语法检查通过。
- 临时 C++ 驱动直接包含生产 resource_server.cpp，20 项解析器/真实临时文件
  重点检查通过：精确 Host、GET/HEAD/非法方法、一次解码、穿越、非法编码、
  MIME、根外 symlink、目录/缺失文件、8 MiB 上限。未增加正式测试目标。
- 临时驱动实际启动 loopback 服务，一次 HTTP GET 返回预期字节和 no-store。
  这是服务冒烟检查，不是 WebView/窗口/模块链路运行证据。
- 独立边界审查修正 NOMINMAX、HEAD 错误响应正文与重复同步根目录检查；
  Linux 原子设置 socket/accept4 的 CLOEXEC/NONBLOCK。`git diff --check` 通过。

未执行本增量的完整 CTest、打包、重复压力或 sanitizer 矩阵；Windows/macOS
编译、真实三引擎模块/fetch/CSP、网络对抗、关闭/导航竞争、目标架构运行与
跨窗口存储隔离仍待统一验收。既有 GUI 运行依赖阻塞没有被此次 HTTP 检查解决。

## 可搬移应用 staging 增量（07:21 UTC）

- Linux x64 源码树配置、实际 Desktop/example 构建及新 CMake staging 目标通过，
  输出包含程序、同一份前端、启动脚本、依赖说明和 SHA-256 manifest。
- 安装 SDK 到独立前缀后，独立示例 find_package、构建、staging 通过；最终收紧
  二进制检查后，已安装 staging 工具再次处理真实 Linux 示例通过。
- 新工具 Node 语法和8项重点测试通过：覆盖拒绝、路径/名称、架构不匹配、
  ELF OSABI/入口、Mach-O macOS/iOS 平台、PE 子系统；合成头测试不是实机认证。
- 资源 helper 的 Linux 严格语法检查通过；临时生产源码 probe 从 cwd=/ 经 PATH
  启动仍定位正确，缺失 web 时明确 resource_directory_unavailable。
- 独立审查修复过宽二进制平台/可执行类型判断；git diff --check 通过。

未运行本增量 GUI、完整 CTest、打包矩阵或压力测试。Windows/macOS 编译和真实
快捷方式/Finder 启动、搬移后的目标运行、外部依赖、签名、公证及安装器仍待
统一验收。应用 staging 不提供系统 WebView，不改变已记录 GUI 依赖阻塞。
