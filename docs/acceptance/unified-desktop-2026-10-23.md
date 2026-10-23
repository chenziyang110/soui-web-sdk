# PR #103 六纵切集中限定验证：2026-10-23

代码基线：[远端提交 224276096847e10a53250f46756c8aa941e9e535](https://github.com/chenziyang110/soui-web-sdk/commit/224276096847e10a53250f46756c8aa941e9e535)。
19 个改动文件远端 blob 与本地冻结快照 `de23e8f38c868a383829c0ea6cd4c9f9d9f332ee` 全部一致，
对应阶段二打开/保存路径选择完成的源码。
本记录不修改生产 acceptance.json，不构成 releaseReady、签名、安装器或全架构通过结论。

## 实现范围

| 纵切 | 当前实现 | 未完成的验收 |
|---|---|---|
| 窗口状态 | 三后端 minimize/maximize/restore、观测状态和事件、网页显式授权 | 实际窗口管理器状态、焦点/DPI/IME |
| 可取消关闭 | 宿主同步审批、整组预检、重入屏障；程序 close/quit 保留 force | 三端标题栏/系统退出、未保存业务交互 |
| 受控服务 | 当前窗口属性、剪贴板读/写、目录选择独立默认拒绝，typed facade | 最新三端真实剪贴板和模态交互 |
| 层级菜单/托盘 | 64 节点/4 层、禁用祖先与命令修订隔离 | 目标桌面菜单与托盘宿主行为 |
| 全局快捷键 | 宿主注册，Windows/Carbon/X11，窗口所有权及注销屏障 | 真正注册、冲突、触发、释放；本轮未操作用户全局热键 |
| 打开/保存路径 | 三端 native chooser、独立权限、明确取消和路径限额 | 目标对话框交互；选择路径不授予读写，不执行文件操作 |

## 云端本次实际执行

Linux x86_64 / GCC，真实 GTK 3.24.49、WebKitGTK 2.52.6 开发头文件来自既有隔离依赖。
只运行下列限定组合，不重编 WebKit，不运行远程 CI，不冒充原生 GUI。

1. 冻结源码重新编译共用层，并完整执行一次 fake contract：**1321 checks，0 failures**。
   覆盖六纵切与既有公共生命周期/Bridge 契约，不调用三端原生 UI。
2. Node 五个工具/adapter 文件：**39 pass**；真实 preload 生成脚本在 Node VM：**17 pass**。
   合计 **56 pass、0 fail、0 skip**。初次组合命令漏传 contract 路径，preload 在启动前失败；
   正确传入后只补跑该套，保留原始调用错误，不将其算成产品断言失败或隐去。
3. Linux 后端使用 `-fsyntax-only -Wall -Wextra -Wpedantic -Werror`，X11 宏开启/关闭各一次通过。
   初次将外部头文件作为普通 `-I`，glibc 的 `#include_next` 在 pedantic 下触发错误；
   改为标准 `-isystem` 分类外部依赖后通过，未修改或关闭 SDK 警告。
4. CMake 3.31.10 / Ninja 全新 desktop 配置通过（`SOUI_BUILD_TESTS=OFF`），
   生成的安装规则包含 helper、6 个工具、模板、adapter 和声明，共 10 项路径检查通过。
   这是配置/安装规则核对，**不是本轮 Linux SDK 构建或安装执行**。

### 可复现命令

在仓库根目录，将 `WORK` 设为独立临时输出目录：

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
  -Isdk/include -Ithird_party/include/json tests/desktop/contract.cpp \
  sdk/src/desktop/{application,protocol,assets,asset_executor,resource_server}.cpp \
  sdk/src/runtime/platform/secure_random.cpp -o "$WORK/contract"
"$WORK/contract"
node --test tests/desktop/frontend-adapter.test.mjs tests/desktop/create-app.test.cjs \
  tests/desktop/stage-app.test.cjs tests/desktop/verify-app.test.cjs tests/desktop/archive-app.test.cjs
SOUI_DESKTOP_CONTRACT_EXECUTABLE="$WORK/contract" node --test tests/desktop/preload.test.cjs
```

Linux 语法检查用 `pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 gdk-x11-3.0 x11`，
把依赖 `-I` 选项转换为 `-isystem`，添加 `-Isdk/include -Ithird_party/include/json`；
对 `sdk/src/desktop/backends/linux_webkitgtk.cpp` 分别省略/设置
`-DSOUI_DESKTOP_X11_SHORTCUTS=1`。隔离依赖需要准确设置 PKG_CONFIG_SYSROOT_DIR/LIBDIR，
不得将本地测试绝对路径写入交付工程。

CMake 使用 `cmake --preset desktop -B "$WORK/cmake" -G Ninja -DSOUI_BUILD_TESTS=OFF`；
本环境显式指定已有 Ninja 路径。检查生成的 sdk/desktop/cmake_install.cmake 中
SouiDesktopApplication.cmake、stage/sync/preflight/verify/archive/create 工具、模板及两个 frontend 文件。
工具套件包含合成 ELF/Mach-O/PE 元数据和临时目录，不能等同实际目标二进制启动。

## 第六纵切 macOS 原生编译

Mac 子任务核对 9 个增量文件 blob 与上述基线一致。显式 Xcode SDK 配置退出 0，
`cmake --build out/desktop-universal2 --target soui_desktop soui_desktop_example soui_desktop_contract --parallel 1`
完成 17/17、退出 0，包含打开/保存对话框 Objective-C++ 后端与新增系统框架链接。
静态库 `lipo` 包含 x86_64、arm64；ARM 宿主执行 `--file-dialogs` 为 351 checks、
0 failures。这是 fake backend 契约，不打开真实面板、不读取/写入用户文件，不验证 GUI。
环境与显式 SDK 路径见 [Mac 环境记录](../macos-native-validation-20261023.md)。
### 最终已安装 SDK → 独立 universal2 消费者

最终执行前核对 adapter：`desktop.mjs` blob 为 `863ee15c30a7e4c919386b056b2e66c851458c27`，
`desktop.d.mts` 为 `cf070f71780edf2d915f21da2868ff906119f7c2`。
源码、安装 SDK、生成项目 web/soui 中这两个文件逐字节/散列一致，安装头文件同源。

- 新安装目录 `out/desktop-sdk-delivery-final`：53 文件，7,020,830 字节。
- 新生成目录 `out/desktop-delivery-consumer-final`：12 文件；配置显式指定
  `-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'`，库及消费者 lipo 均有两个 slice。
- 安装、生成、配置、构建、stage、verify、archive 全部退出 0。
- `out/desktop-delivery-staged-final-universal2`：12 个 manifest 文件，1,406,037 字节。
- `out/desktop-delivery-archive-final-universal2`：ZIP 1,414,285 字节，SHA-256
  `7167f787d8ea585a5ab3dc2fb421861ec7369e8355c93f2018286887e4e0db63`，sidecar 一致，
  `integrityVerified=true`、`nativeRuntimeVerified=false`。没有签名或真实性认证声明。

保留前置执行失误：首次未同步最新 adapter，使用旧 frontend 生成 arm64 消费者；
首次错误标记 universal2 被校验拒绝。旧输出保留不覆盖，正确同步并在全新目录显式
选择双架构后才获得以上最终结果。此为执行接线错误，不据此报告产品源码缺陷，
也不将旧包数字混入最终通过结果。未运行生成程序 GUI、真实对话框/热键或 Intel slice。

## 平台/架构事实矩阵

| 平台/架构 | 实现/目标策略 | 当前原生证据及缺口 |
|---|---|---|
| Linux x64 | GTK/WebKitGTK；X11 快捷键按依赖启用 | 历史真实 GUI/Bridge/ESM/fetch 已过；当前六纵切仅公共回归及后端语法检查，运行时快照丢失，未重跑 GUI |
| Linux ARM64/x86/ARM32 | 架构策略存在，32 位取决发行版完整依赖 | 未目标编译/运行；不能由 x64 或合成矩阵推断 |
| macOS arm64/x64/universal2 | WKWebView，现代 64 位目标 | 第六纵切双 slice 原生构建、ARM 宿主 fake 351/0；非 GUI，Intel slice 未运行；最终最新 SDK/adapter 的双架构消费者安装交付链已过，未验证 GUI |
| Windows x86/x64/ARM64 | WebView2 实现及目标策略 | 尚无匹配 Windows SDK/MSVC 目标编译或运行证据 |
| Windows ARM32 桌面、macOS 32 位、ARM64EC | 不作为此统一桌面支持目标 | 明确拒绝/不支持，不标未测试即可支持 |

Linux Wayland（包括处于 Wayland 会话中的 XWayland）及未知会话的全局快捷键明确
`not_supported`，尚未实现 Portal。应用内 accelerator、任意文件读写/持久授权、
自动 updater、安装器、签名/公证和完整 Electron API 等价不是本轮已实现能力。
服务能力可用不等于特定快捷键/窗口管理请求必成功，页面预授权不等于真实系统手势。

## 收口状态与下一步

阶段一/二六项核心代码已实现，最新 Mac 原生构建及安装 SDK → 生成 universal2 消费者
→ stage/verify/archive 已通过。限定集中非 GUI 交付检查完成，但阶段三真实平台验收仍未闭环。
Mac 真实 GUI 曾因 AppKit regular application 激活失败受阻，未确认是源码缺陷；
不重复提权、绕过系统保护或把 fake 运行当 WKWebView 通过。

Linux 历史证据见 [2026-10-22 记录](../linux-native-validation-20261022.md)，
Mac 环境/历史交付与 GUI 阻塞见 [2026-10-23 记录](../macos-native-validation-20261023.md)。
旧记录均保留自己的基线；新路径策略及六纵切不能继承历史 GUI 通过结论。
仍需可用目标图形会话的同业务窗口/菜单/系统服务交互，以及 Windows 和未测架构证据。
在这些门槛关闭前保持 draft；不更改版本/许可、不合并或发布、不声称“一切平台架构完美运行”。
