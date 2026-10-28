# @APP_NAME@

最小 SOUI 统一桌面项目，应用版本 @APP_VERSION@，标识 @APP_IDENTIFIER@。
此版本属于应用自身，不代表 SDK 版本或正式发布。

## 配置与构建

先准备与目标平台/架构匹配的已安装 SouiDesktop 开发 SDK、C++17 工具链、
CMake 3.24+ 和 Node（仅生成/staging 工具需要，最终应用/doctor 不需要）。
SDK 前缀由配置命令传入，项目中不保存开发机绝对路径：

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="<已安装SDK前缀>" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target @APP_NAME@
```

Windows 使用兼容 MSVC 的工具链；macOS 在 project() 前默认最低12.0，若已安装
SDK 要求更高版本，请配置 CMAKE_OSX_DEPLOYMENT_TARGET。交叉编译应传入正确
toolchain，生成器不下载 SDK、不安装依赖、不猜测编译器或系统权限。
使用macOS universal2 SDK时，可配置 `-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"`
生成双架构应用，或只指定一个slice生成薄片应用；staging按应用架构而非SDK
包含的全部slice标记。薄片SDK不能提供另一个架构，仍需目标Mac实际编译和运行。

运行构建目录中的 @APP_NAME@（Windows 为 .exe，多配置生成器通常放在 Release
子目录）。程序通过 SDK 获取实际可执行文件旁的 web，不依赖启动工作目录。
每次构建应用目标均同步受管 web，源中已删除的文件也会从旧受管副本移除。
工具拒绝接管已有无清单目录，拒绝手工改写/新增文件或链接；不要把用户数据放在
构建 web 中。旧模板迁移请选新的构建目录，不要删除所有权清单来绕过校验。
运行中的应用应先停止再构建；这不是热更新机制。交付仍使用下方新目录 staging。

## 最小接入面

- main.cpp 只有一个窗口和 app.info 原生方法 allowlist，不提供任意文件/命令能力。
- 模板显式开启 HTTP 资源服务，SDK 默认行为不变；readAsset 仍默认关闭。
- 前端使用同源 CSP、静态/动态模块和 JSON fetch，无 CDN、内联脚本或包安装。
- 保持前端相对 URL；绝对 /assets 路径不适用于窗口随机前缀。资源目录应为可信、
  不可变的应用资产，不能混入密钥或将同源不可信脚本视为权限隔离主体。

## 前端类型与调用

web/soui/desktop.mjs 是 SDK 的薄适配层，app.mjs 已通过它调用 app.info。相邻
desktop.d.mts 提供可选 TypeScript/编辑器提示，不要求安装包或编译 TS。可用
invoke、on、readAsset 和 assetBytes/assetText/assetJson；泛型只提供静态提示，
不代替业务结果校验。无原生宿主立即报错，不模拟成功，也不增加 Bridge 权限。
模板仍未启用 readAsset，需应用显式选择；普通同源资产继续使用 fetch。

## 应用目录与诊断

```sh
cmake --build build --config Release --target @APP_NAME@_stage
```

生成 build/staged-Release（配置名取决于生成器）。输出必须是新目录，重复 staging
不会覆盖。可重新配置 `-DAPP_STAGE_DIRECTORY="<新的输出目录>"`。

Linux 使用目录内 launch.sh；独立诊断为 `./launch.sh --doctor`。Windows 运行
程序，诊断为 `powershell -NoProfile -File doctor.ps1`，不修改执行策略。
macOS 输出 .app，独立诊断为 `/bin/sh doctor.sh`。manifest 与 DEPENDENCIES.txt
记录复制文件散列和外部依赖要求。

这不是自包含安装器：Linux 需要兼容 GTK/WebKitGTK 及 helper，Windows 需要
WebView2/VC runtime，macOS 使用系统 WebKit。诊断的 unknown 或退出0不证明
依赖已安装、GUI 可运行或架构兼容；目标运行、三引擎模块行为、签名/公证、
部署许可和完整回归仍需验证。SDK/第三方许可不会由生成器复制为应用许可，
分发前须确认适用授权和必要声明。不要关闭沙箱或改变安全设置来伪造通过。

## 校验已有交付目录

当前 stage v2 会保留 SDK 的 `LICENSE.md`、Portable `THIRD_PARTY_NOTICES.md`
原始字节；Windows 另带构建所用 WebView2 包的 `LICENSE.txt` 和 `NOTICE.txt`。
它们放在包内 `legal/`（macOS 为 `.app/Contents/Resources/legal/`），不由 web
资源服务暴露。来源由 SDK 的 CMake 目标显式传入；安装包搬迁后仍从该 SDK 取用，
缺文件时拒绝暂存。ZIP 包含同一清单内的 notice。固定集合不涵盖应用额外依赖的
全部许可审计，也不改变开发级、未签名、未完成目标系统验收的状态。
旧 v1 校验成功仅代表旧完整性契约，不证明 notice 交付。

`cmake --build build --target @APP_NAME@_verify_stage --config Release` 只读校验现有
staging 目录，不重新构建或覆盖它。stage目标创建产物后也会自动执行此校验。
输出是结构化结果；文件与自附SHA-256清单一致不等于签名或来源可信，也不表示
系统依赖与窗口运行已验收。不要把报告写入被校验目录，否则会成为额外文件。

## 创建交付归档

已有staging目录校验通过后，构建 `@APP_NAME@_archive_stage`（多配置构建同样传
`--config Release`）。默认产生 `<staging目录>-delivery`，其中有ZIP、SHA-256和
交付清单。归档目标不重新构建或覆盖staging，输出须为新目录；自附散列不是签名，
此步骤也不会自动发布或验证真实窗口运行。详见SDK应用staging文档。
# 安装模板完整性

输出目录不得与模板树或 SDK frontend 适配源树重叠；生成器在创建任何目录前
拒绝此类输出，避免新项目污染后续生成所需的 SDK 源文件。

生成器在创建输出前检查 CMakeLists.txt、main.cpp、startup_report.h、startup_dialog.h、
startup_dialog.cpp、startup_dialog_macos.mm、startup_trace.h 及 web 下的 index.html、
app.mjs、feature.mjs、details.mjs、config.json、style.css。缺失必需依赖时
报告缺失路径并拒绝写出半成品；README 等可选文档不作为运行依赖。

## Linux 链接搜索路径

本模板 Linux staging 显式使用 CMake 安装期 RPATH 策略，默认不把开发依赖目录
自动写入应用 RUNPATH。该策略也影响构建目录直接启动；使用用户前缀依赖时需
自行提供正常运行环境。可在 CMake 中显式设置目标 INSTALL_RPATH，但相应库与
helper 的部署仍由应用负责。它不会打包 WebKit、清洗所有绝对链接路径或使产物
变成无依赖分发包；其他平台的链接行为不变。

## 启动失败与便携报告

新生成的应用在启动失败时向 stderr 输出去敏诊断，并尽力显示一次原生错误框。
运行期 error 回调的首个去敏错误立即写 stderr，后续不重复；退出清理后才尝试原生提示，
不会在回调中嵌套弹框。正常启动不弹框；无显示会话或系统拒绝原生提示时仍只有 stderr，不保证任何环境都可见。
该提示不自动安装运行库、修改权限或切换资源协议，也不证明启动根因已解决。

无需 Node 的应用内诊断入口为 `@APP_NAME@ --diagnose`；Mac bundle 可运行
`@APP_NAME@.app/Contents/MacOS/@APP_NAME@ --diagnose`。输出一条
`soui.desktop.startup/1` JSON，可由调用者显式重定向后分享，不自动持久化或上传。
报告仅含固定阶段、白名单错误码以及允许的监听器原生数字错误，不输出任意错误消息、
资源 URL、能力令牌或用户路径。`--diagnose` 使用隐藏窗口完成有限初始化后关闭，
不安装业务回调、不运行正常应用事件循环、不调用剪贴板/选择器/全局快捷键。
它仍会创建临时 loopback listener 与隐藏原生 WebView，不是纯静态检查；
退出 0 不代表页面/Bridge/GUI/原生运行时验收通过，失败非零，未知参数退出 2。

如果程序连 main 都没进入（动态库缺失或系统拒绝执行），应用不能自己弹框或报告，
使用交付目录现有 doctor 入口。Linux 原生错误框使用既有 GTK3，Windows 使用 user32，
Mac 模板额外编译 Objective-C++17/ARC helper 并链接系统 AppKit。
旧生成项目不会被覆盖；需重新生成到新目录，或人工迁移 main、startup_* 源及 CMake 接线。

## macOS 显式启动阶段取证

仅诊断构建可开启 `SOUI_APP_STARTUP_TRACE`（默认 OFF），同时显式指定
`SOUI_APP_STARTUP_TRACE_FILE` 为绝对路径。非 macOS 目标拒绝开启；路径不得包含
控制字符或分号。配置将路径写入本地构建头和诊断二进制，不添加公共业务 API、
默认日志路径或环境变量 fallback，不能把此类二进制作为正式分发产物。

运行前由操作者在可信任务目录独占创建一个新的空普通文件，精确权限 0600，
当前用户所有且只有一个硬链接。文件与祖先目录不得可由不可信方修改。应用只用
append/no-follow 打开现有文件并持有非阻塞独占锁，不创建文件或目录、不截断、不
chmod；已有内容、权限/身份/链接不符或锁被占用时直接停止取证，不换路径或重试。
固定阶段每进程只写一次，每次写前复查文件，累计上限 16 KiB；不记录任意错误原文、
路径、PID、URL、令牌或网页数据，也不上传。协作锁不防御同 uid 的外部恶意改写。

SDK 内部阶段另由 SDK 构建的 `SOUI_DESKTOP_STARTUP_TRACE` 与
`SOUI_DESKTOP_STARTUP_TRACE_FILE` 显式开启，必须使用另一个独占创建的空文件；
SDK 配置不会传给应用。同一路径只会使后打开的组件拒绝记录，不会覆盖既有记录。
需要同时取证时分别配置、分别准备文件，不能复用上轮非空文件。

模板记录 main、initialize/run/quit/析构以及错误提示的进入/返回，错误框进一步记录
AppKit 初始化、屏幕查询和 modal 调用。缺少 return 只定位最后可观察调用，不证明
死锁；无记录也可能是未进入 main 或文件校验拒绝。`--diagnose` 可产生同类记录，
但隐藏命令行初始化不能替代正常桌面启动上下文，不证明 Finder 无窗口根因已解决。
默认 OFF 时标记为空操作，不改变激活策略、事件循环、监听器或权限。

Mac 错误框在进入同步 modal 前还要求应用当前活跃；非活跃时保留 stderr 并退出，
诊断构建记录 `dialog_inactive_skip`，不强制激活。该瞬时守卫不保证随后可见，
不能排除检查后失焦，也不是原先无窗口问题已修复的证据。
