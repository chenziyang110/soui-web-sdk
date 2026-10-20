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

- Linux 真实窗口和 JavaScript/Bridge 集成：当前执行环境不能创建 Xvfb/D-Bus
  所需 Unix socket；受支持的提升执行入口也无法初始化。未绕过权限或关闭 WebKit
  sandbox。原生程序在没有显示服务时返回 `E_DISPLAY`，该失败不是测试通过。
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
