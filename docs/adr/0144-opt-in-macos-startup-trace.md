# ADR-0144：仅诊断构建的 macOS 启动阶段取证

状态：接受；默认关闭，不是公共 Desktop API。

## 背景

最终生成应用正常桌面启动后用户看不到窗口，自动化 AX 查询超时；普通命令行
`--diagnose` 则在 AppKit 创建前返回 listener bind/EPERM。两种上下文不能混为
同一根因，且正常 LaunchServices 启动的 stderr 不一定可取得。需要定位实际进入
与返回的调用，不以反复启动、权限变化或猜测死锁代替证据。

## 决定

仅 macOS 的显式诊断构建启用固定阶段 trace。SDK 使用
`SOUI_DESKTOP_STARTUP_TRACE` 与 `SOUI_DESKTOP_STARTUP_TRACE_FILE`；生成模板使用
`SOUI_APP_STARTUP_TRACE` 与 `SOUI_APP_STARTUP_TRACE_FILE`。开关默认 OFF；开启时
路径必须显式为绝对路径。两者必须指定不同任务文件，构建路径不自动传播到安装
消费者。路径只存在本地构建配置和诊断二进制，不写进报告或仓库实例。

每个组件只打开预先独占创建的空文件，不创建文件或目录，不截断、不 chmod。
以 O_APPEND/O_NOFOLLOW/O_CLOEXEC 打开；非阻塞独占锁持有到进程退出，拒绝
已锁或非空文件。fstat 要求普通文件、当前 uid、单硬链接及精确 0600 权限。
每次写前再次检查身份/权限/大小和 16 KiB 上限（包含本条）。固定枚举阶段每个
进程只输出一次，单次有界 write，失败不重试、不换位置、不输出任意文本。
不含路径、PID、URL、令牌、错误原文或网页数据，无后台日志线程或上传。

SDK 与模板保留私有实现，策略内容精确一致并受专项检查，避免公共 API 或安装
头契约扩张。默认构建的调用为空操作，不改变 socket、激活策略、事件循环或权限。
文件和祖先目录必须可信且不可由不可信方修改；锁是协作进程约束，不承诺防御
同 uid 的外部恶意改写或提供完整文件系统沙箱。诊断构建不可当正式分发产物。

## 备选与验证

只写 stderr 无法保证正常桌面上下文取证；默认持久日志、自动 fallback 路径及
修改系统日志/安全配置不采用。使用两个明确文件，避免 SDK 和模板首次打开互相
冲突；同一路径会使后打开的组件失效，不会覆盖已有记录。

云端只验证固定阶段/限额/权限/链接/锁/非空拒绝和默认关闭，Mac 编译与实际正常
启动取证由目标环境单独执行。缺少 return 标记只能定位最后可观察调用，不能单凭
它确认死锁；无记录可能是未进入 main、文件检查拒绝或执行上下文差异。
这只增加定位能力，不声称已解决 Finder 无窗口根因。

## 使用与取证边界

在可信任务目录中以独占创建方式准备两个不同空文件，权限 0600，不重用既有记录。
重新构建 SDK 时显式传入 `-DSOUI_DESKTOP_STARTUP_TRACE=ON` 和
`-DSOUI_DESKTOP_STARTUP_TRACE_FILE=<SDK任务文件绝对路径>`；安装到独立诊断 SDK
前缀。生成的新消费者配置另外传 `-DSOUI_APP_STARTUP_TRACE=ON` 和
`-DSOUI_APP_STARTUP_TRACE_FILE=<应用任务文件绝对路径>`。诊断 SDK 静态库本身已
含取证代码，不能把“不导出 CMake 定义”理解成该库被消费者链接后自动关闭取证。
恢复默认应重新以 OFF 构建干净 SDK 和消费者，不能将诊断安装前缀当发布 SDK。

SDK 和应用文件使用相同 `SOUI_STARTUP_TRACE/1` 固定行格式，但各自只有局部调用
顺序，不记录时钟，不可按两文件拼接推断跨文件全局时间顺序。阶段 return 只表示
同步调用返回，不代表成功或页面完成；`mac_run_enter` 而无 return 在正常事件循环
运行时也是预期，不是卡死证据。每个阶段只记首次，不能用于后续窗口/导航分析。
取证必须绑定具体构建、启动方式和文件；shell 与 LaunchServices 的结论不能互换。
文件检查失败不会回退到 stderr 或其他位置，不能把空文件单独解释成未进入应用。

## 2026-10-24 限定验证与当前阻塞

实现基于远端 `5f018247eb0c48f179ce3cac1f91b45f1bb9f9f9`，Mac 收到的 10 个
构建必需源文件逐 blob 匹配云端冻结代码。小补丁 gzip 为 7,181 字节，SHA-256
`44a50cf3a3166b8a629cb3975dddeacc3cb8b1a07a2796be06fac61ca70759d3`。

云端 sink 专项覆盖 12 个隔离场景；开启、默认关闭、显式 0、非 Apple 保护及独立
应用 namespace 均通过。生成器 6/6（含两份私有 header 字节一致）、模板 CMake
片段 73 项、既有 main fake 报告检查、application.cpp 严格语法与 diff 检查通过。
没有新增全量回归或 CI。两个开关默认 OFF，compile definitions/include 均为 PRIVATE；
生成的 trace 配置头不安装，SDK 配置不导出任务路径。诊断静态库本身仍含已编译的
取证路径，必须使用独立前缀，恢复正常分发须重新以 OFF 构建。

Mac 使用已有记录的 SDK 26.5 和显式 arm64/x86_64 架构完成诊断 SDK 及全新
StartupTraceConsumer 的 Release universal2 编译、staging 与独立 verify，全部退出 0；
12 个 manifest 文件，共 1,443,791 字节。此为编译/完整性通过，不是运行通过。

随后仅一次正常 `/usr/bin/open` 启动确切新 app，返回 1，LaunchServices 报
`kLSNoExecutableErr (-10827)`；两份 trace 均保持 0 字节。此时 CUA 工具不可用，
未改用直接执行或提升权限重复启动。只读核对确认：绝对路径及大小写匹配，无符号
链接；plist lint 通过，CFBundleExecutable 为 StartupTraceConsumer，APPL 与
org.soui.startuptraceconsumer 正确；exe 为 0755，父目录可遍历；两个 slice 的
最低系统为 12.0、构建 SDK 为 26.5。未发现 LC_RPATH 或非系统私有依赖引用。

`codesign --verify --deep --strict` 返回 1，bundle 未签名；Mach-O 为 ad-hoc、
无 Team ID，Info.plist 未封装。这里只记录检查事实，不据此认定 LaunchServices
失败根因，不自动签名或修改安全设置。尚未确认进程进入 main，也未获得真实窗口、
错误提示或 Bridge 运行证据。用户正常 Finder 启动后的阶段记录仍待取得；空 trace
不能区分未进入代码和 sink 拒绝写入。本次只交付可观测性，不宣称根因修复。

## 后续结果（保留上述旧包事实）

之后工具可用性恢复，正常 CUA 启动旧 StartupTraceConsumer 获得阶段记录：
已经过 listener/sharedApplication/activation，初始化返回并完成析构后进入
`dialog_modal_enter`，未返回；这次不是此前 shell 的 bind/EPERM 路径。
据此实施的观测激活策略修复，在独立新消费者完成限定真实窗口、Bridge、模块/
同源 fetch 和正常关闭验证，见 [ADR-0145](0145-observed-macos-activation-policy.md)。
上述空 trace、LaunchServices 错误及未签名检查仍为各自旧次操作的事实，不覆盖或
改写为成功，也不将新候选的最小实跑推广为全能力或其他平台验收。
