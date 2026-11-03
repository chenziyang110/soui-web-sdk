# ADR-0122：可移植 Runtime 与桌面后端边界

- 状态：提议，实现随本 PR 提供；目标机矩阵待验收
- 日期：2026-10-20

## 背景

原生入口无条件启用 Windows 资源编译器，SDK 将全部实现编入一个
Windows/MSVC 目标，应用仅按指针宽度选择 x86/x64 预编译依赖。这会把
ARM64 误当作 x64，也阻止不依赖窗口的任务、权限、事件和 Bridge 协议
在 Linux、macOS 上使用。仓库中的 SOUI/Miniblink 桌面交付只有 Windows
x86/x64，不能通过删除平台检查获得其他平台的窗口实现。

## 决定

新增显式选择的 `SOUI_BUILD_PORTABLE` 构建档位及 `Soui::Runtime` 静态目标，
编入现有可移植 Runtime 实现。默认 Windows 桌面构建保持 `Soui::Sdk`
和既有 API、最低 Windows、MSVC 及 CRT 契约。独立安装采用 `SouiRuntime`
CMake 包，只导出此档位实际实现的头文件和目标，不安装 Windows Host、
UIA、本地命名管道或桌面服务的不可用 API。

平台探测根据目标编译器、CMake 目标和指针宽度交叉核验，不使用宿主
CPU 或指针宽度单独猜测架构。支持的可移植目标为 Windows/Linux 的
x86、x64、ARM32、ARM64，以及 macOS 的 x64、ARM64 和双架构 universal2。
工具链不具备该目标时失败；未执行的目标不标为已验证。未知架构、
ARM64EC、macOS 32 位和不匹配的指针宽度明确拒绝。

权限令牌仍需系统密码学安全随机数：Windows 保留 BCrypt，Linux 使用
getrandom 并处理短读和有界中断重试，macOS 使用 Security.framework 的
SecRandomCopyBytes。失败关闭，不使用弱随机或悄悄退回其他来源。
严格 UTF-8 校验在非 Windows 实现相同的有效 Unicode 标量边界。
时钟沿用现有 `steady_clock` 微秒诊断语义。GUI 协议协调器不创建系统
窗口，其随机文档身份可复用系统熵；真正窗口和 renderer 不纳入 Runtime。

维护者仍按现有约定运行本地验证，不添加远程 CI，不改变验收结论。

## 备选方案

- 仅放开 `WIN32` 检查：会在系统头文件/链接阶段失败且掩盖依赖缺口，拒绝。
- 模拟 Win32 或返回空成功：破坏权限和生命周期语义，拒绝。
- 引入第三方跨平台 WebView：涉及窗口线程、导航、输入、资源加载、IPC、
  安全和第三方许可，需独立 ADR 与目标机验收，本 PR 不替换固定依赖。

## 兼容性与迁移

既有 Windows 桌面构建方式保持默认。跨平台调用者显式请求 Runtime 并链接
`Soui::Runtime`，负责提供可信输送通道和原生身份验证；权限对象本身不是
操作系统端点认证。Runtime 与 Sdk 是替代构建档位，不应将两份相同 Runtime
实现同时链接到一个程序。安装包不改变冻结的 0.5.0 发布物或承诺稳定 C++ ABI。

## 验证

覆盖目标矩阵、错误路径、密码学熵失败、非法 UTF-8、任务配额、取消、
会话撤销、Bridge 授权和安装后外部消费者。Linux 本机实际编译/运行结果
在 PR 报告，Windows/macOS/ARM 与真实桌面保持待验收；不得以矩阵单测
代替这些平台的实机运行。回退可撤销本 PR，不触碰原有发行包。
