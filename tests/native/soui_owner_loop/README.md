# 真实 SOUI owner-loop 消费者

本目录只准备源码，尚未配置、编译或运行。它通过正常公开 `soui_owner_application`、真实 `SApplication/SMessageLoop/IMsgLoopFactory` 和正常 `SComMgr::CreateRender_GDI` 接入，不复制 SDK 调度器、不定义替身 SOUI 类或 singleton 存储，也不重编 vendor。

一个 fresh app 内仅有三项有限行为：

1. 真实主 loop 的 filter 消费自有消息，原 idle 只执行一次，再由 message-only driver 的实际 WndProc 发 WM_QUIT；核退出码和运行标志。
2. 在实际主 loop dispatch 内，由应用当前 factory 创建不同的 nested loop，运行它自己的 filter/idle；nested Quit 后主 loop 仍运行，再主 loop Quit；核 SOUI 原有 last-message wParam 返回语义。
3. 销毁 driver、nested loop 和 app 后，核实际 DLL imported App/Notify singleton 为空、本线程顶层及 message-only window 集合恢复。这里只证明这些可观察项，不宣称完整 handle/内存无泄漏。

没有 visible window、ShowWindow、激活、focus、用户输入、浏览器或性能采样。正常优化器 status 必须成功，不能以静默 legacy 回退当作正常接入通过。异常返回非零；CTest 原期限15秒，不重试或延长。

顶层窗口允许合法空集：[EnumThreadWindows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumthreadwindows#return-value) 在未找到窗口时也返回 FALSE。夹具先清错误状态、单独检查 callback overflow；FALSE 只在零回调且未观察到错误时作为空集，部分枚举或错误仍拒绝。随后继续原 message-only 枚举和最终集合比较，不要求环境存在额外窗口。

## 独立构建输入

独立工程需要已经正常安装且真实提供该公共头/实现的 SDK。显式给 `SOUI_OWNER_LOOP_SDK_ROOT`（绝对 prefix）和 `SOUI_OWNER_LOOP_SDK_VERSION`（完整 raw release）；使用现有 raw/numeric EXACT/header helper核验。SDK 不存在时只准备，不构建替代品。根工程以后接入时可以消费其正常 `Soui::Sdk` target；本次没有修改根 CMake。

SOUI include/lib/DLL 默认取仓库相对 `third_party/`，可用显式 `SOUI_OWNER_LOOP_DEPENDENCY_ROOT` 指向相同布局的真实依赖。架构随实际编译目标选择 x86/x64，部署复用 `soui_dependency_tests` 的正式 SOUI/CRT DLL集合。单个消费者 TU 链接正常 SDK 和现有 `soui/utilities` 导入库，不从 vendor 源码造目标。

从仓库根配置的参数示例（仅说明，尚未执行；实际构建须通过既有编译准入与生命周期入口）：

```powershell
cmake -S tests/native/soui_owner_loop -B out/soui-owner-loop-x64 `
  -G "Visual Studio 17 2022" -A x64 -DCMAKE_SYSTEM_VERSION=10.0.19041.0 `
  "-DSOUI_OWNER_LOOP_SDK_ROOT=$env:SOUI_MATCHED_SDK_ROOT" `
  "-DSOUI_OWNER_LOOP_SDK_VERSION=$env:SOUI_MATCHED_SDK_VERSION"
```

目标为 `soui_owner_loop_consumer`，测试为 `sdk.native.soui-owner-loop.consumer`。后续由任务负责人审核、集成根入口并实际执行；源码或链接成功不能替代行为运行、正常收口或 SDK 性能验收。

## ABI 与来源

MSVC 在引入导出派生类前，沿用 `tests/native/soui_dependency_tests.cpp` 对 `SSingleton<SApplication/SNotifyCenter>` 的 `extern template __declspec(dllimport)` 声明。测试读取的是原 DLL 的 base-template 数据，不写 `ms_Singleton` 定义；Clang 路径仍用现有头文件提供的导入声明。正常 GDI factory 与 DLL目录设置来自 `templates/desktop-app/app/main.cpp`，factory循环与公共 glue来自正常 SDK 头。该消费者不是以上历史测试通过记录的新身份，当前执行状态仍为未验证。
