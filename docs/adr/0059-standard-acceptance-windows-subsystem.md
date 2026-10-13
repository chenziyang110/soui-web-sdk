# 0059：标准桌面验收消费者采用 Windows 子系统

状态：候选；未采用、未编译、未运行。

## 原因与范围

当前 Console 验收消费者的四个真实短模式已经自然退出，但实际 Owned Job
均观察到主体与 conhost 两个成员。资源协议要求一个实际主体，其 total1 门
不能对这些 Console 记录开放；已有 resource100 packet 保持未执行且不合格，
不会忽略 conhost、裁剪成员或将原四短记录改成资源通过。

本候选只改变 tests/acceptance-app 的应用子系统与标准启动入口。它不修改
SDK、Host、线程/进程监督、真实业务、关闭期限、资源 policy 或普通参数 schema。
修改之后是否仍产生其他 Job 成员必须重新实际观察，不能从 WINDOWS PE 值
推导 total1，也不能把旧编译 partial 或两成员结果升级。

## 决定

CMake 的真实目标使用 add_executable(SouiAcceptance WIN32 main.cpp)，由正常
MSVC CRT 选择 wWinMainCRTStartup；不覆盖 /ENTRY、不调用 Console 管理 API。
原 wmain 的完整应用体移为内部 run_acceptance(argc, wchar_t**)；wWinMain 仅传入
<cstdlib> 声明的 MSVC __argc / __wargv。不重新用 CommandLineToArgvW 解释原始
字符串，避免改变 CRT 引号、反斜杠、argv[0] 或宽字符处理。

固定 14.44.35207 CRT 的 exe_common.inl 表明 WMAIN 和 WWINMAIN 都采用相同
__scrt_wide_argv_policy，其 configure_argv 调用
_configure_wide_argv(_get_startup_argv_mode())。两者只改变 app_type 与最终入口。
因此该具体编译器的原宽字符参数配置被保留；不是跨编译器 ABI 保证。

OwnedProcessRunner 仍使用原 STARTF_USESTDHANDLES 与选择性继承列表，为主体
传入实际 stdout/stderr 句柄。应用不关闭、替换、重新开启或忽略这些句柄；
原 std::cerr 与 cleanup_failure 的真实 GetStdHandle/WriteFile 路径逐字节保留。
固定 UCRT lowio/ioinit.cpp 会接受实际标准 pipe 句柄；没有可用句柄时不能虚构
已输出诊断，实际负面回归必须检查父方保存的 stderr 字节。

## 回归与实际验证

desktop-entry-cases.json 列出待 Root 执行的真实 CLI 负面及 Unicode/空格路径
回归；它是输入/期望声明，不是已执行结果。负面必须使用新实际 GUI EXE 与
固定 Owned helper，不通过 cmd，不启动业务或创建 report；保留实际 stderr、
PID/创建 FILETIME、自然 root1/supervisor1 与 Job 闭合结果。错误退出永不记作
generic pass。

新正常安装消费者须复跑原四短模式，检查实际 PE WINDOWS / 本架构、正常
SDK 安装/CL/OBJ/LINK/MAP 绑定、真实参数、原 native run.begin 身份、业务/关闭
日志和整个 Job 自然退出。只有本次真实成员数满足资源策略时，才独立选择
100+10 reload 资源运行并保留原 policy 的 total1/计数/边界。不能凭 Console
短模式、纯测试或 subsystem 值提前授权 resource。

未增加模式、预算、SDK 依赖、隐藏窗口或 process filters。所有 OS、长稳、
完整资源、光学、发布与 broader qualification 仍未关闭；既有失败/partial/
Console 载荷及证据原样保留。候选只做 FS authoring，Root 独占实际执行。

依据：
[__argc / __wargv](https://learn.microsoft.com/en-us/cpp/c-runtime-library/argc-argv-wargv?view=msvc-170)、
[WinMain 标准 CRT 入口](https://learn.microsoft.com/en-us/windows/win32/learnwin32/winmain--the-application-entry-point)、
[SUBSYSTEM](https://learn.microsoft.com/en-us/cpp/build/reference/subsystem-specify-subsystem?view=msvc-170)。
