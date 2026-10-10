# 验收程序线程采样回归

本测试在 Windows/MSVC 下直接包含真实验收程序的 `main.cpp`，调用其中的 `process_thread_count()`。测试先包含 Windows、SDK 和标准库头文件，再替换四个 Toolhelp API 的调用；不会运行验收程序入口、创建 Host 或复制 SDK 实现。

15 个用例覆盖完整与空枚举、当前及其他进程的线程过滤、最小合法结构长度、创建和枚举失败、异常结构长度，以及关闭句柄失败。枚举失败时必须保留原错误，不能返回部分线程数；创建成功后必须恰好关闭一次快照。每例按顺序运行，超时为 30 秒。

在仓库根目录配置测试，并将 `$taskInstalledSdk` 设置为匹配架构的已安装 SDK 路径：

```powershell
cmake -S tests/acceptance-thread-sampler -B out/thread-sampler-x64 -G "Visual Studio 17 2022" -A x64 "-DSouiSdk_DIR:PATH=$taskInstalledSdk/lib/cmake/SouiSdk"
cmake --build out/thread-sampler-x64 --config Release --target soui_acceptance_thread_sampler_tests --parallel 2
ctest --test-dir out/thread-sampler-x64 -C Release --output-on-failure --no-tests=error
```

Win32 使用独立构建目录、`-A Win32` 和 Win32 SDK。测试通过 `find_package` 使用公开的 `Soui::Sdk`，编译选项为 `/MD`、`/U_DEBUG`、`/EHsc`、`/utf-8`；该独立入口不挂入源码 SDK 的原生测试父目录。

`SOUI_ACCEPTANCE_MAIN` 默认指向相邻的 `../acceptance-app/main.cpp`。验证冻结源码时，可用 `-DSOUI_ACCEPTANCE_MAIN:FILEPATH=...` 指定绝对路径，并保留源码散列及实际编译、链接记录。测试入口不绑定开发机路径、历史散列或特定编译器版本。

这些故障用例验证线程采样的错误处理和资源关闭。系统兼容性、真实资源增长和长期运行仍由各自验收测试确认。
