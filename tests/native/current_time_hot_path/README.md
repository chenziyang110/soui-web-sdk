# MiniBlink currentTimeImpl 热路径行为夹具

对应 [ADR0145](../../../docs/adr/0145-miniblink-steady-clock-utc-read.md)。显式LLVM22/MSVC142 x86/x64已各执行一次实际行为RED，候选各5场景全部通过；完整内核/真实消费者仍未重编。下列为复现入口；人工FPS采样期间暂停构建。

`CurrentTimeImpl.original.cpp.hex` 是原生产文件完整字节的十六进制参考，解码 SHA256 为 `83de811fe28ef57f6b0e0a73614ef81925c08097288be9f4de4c8eddc06c8d96`。来源为仓库 `third_party/sources/miniblink/content/web_impl_win/CurrentTimeImpl.cpp`，保留其完整 Apple/Google/Torch Mobile 版权与 BSD 条款；可读副本见 [LICENSE.original.txt](LICENSE.original.txt)。它只是冻结对照，不是候选函数的替代实现。当前文件仅认可原 SHA 或已审核局部重排候选 SHA `28e65698db58df2261a054bce73b67b86c692e21ddcaa948e9d611e72fe58495`，其他源码须显式审查后更新绑定。

配置阶段从参考完整字节与当前完整文件按唯一 ASCII namespace 边界提取实际生产函数及 helper，在不同 namespace 内直接编译。完整文件、原编码 namespace 片段和严格 CP936 往返等价 UTF-8 编译投影分别保存在构建目录 `generated/`；`source-binding.json` 记录完整源、片段、投影的 SHA256 和字节范围，同时核验在用函数以外字节不变。夹具编译的是 namespace 片段及测试 shim，不是完整生产 translation unit；不覆盖原头文件、真实 Windows 计时 API 或 CRT 时区行为。

Win32 SDK 提供真实类型；GetSystemTime、mktime、QPC、GetTickCount 和 timeBegin/EndPeriod 仅替换为确定性观测边界，不调用真实精度策略，不设置系统时钟，不启动 GUI。mktime shim 只检查已经初始化的 tm 字段，故现有 tm_isdst、时区、共享 static 并发风险没有被修复或验收。每个 CTest 场景单独进程，避免 QPC availability 和函数 static 状态串场。

首次显式LLVM22/MSVC142 x64编译在原生产 `LARGE_INTEGER = { 0 }` 的union零初始化命中clang `missing-braces` / `missing-field-initializers`，尚未执行行为RED。仅在两份原字节导入片段外push/pop这两条样式诊断，生产字节和 `/W4 /WX` 保持；失败日志另存，不将编译失败冒充所需行为失败。

五个场景覆盖：

- `steady`：原函数每个稳定样本仍读 UTC，候选必须零次；首次同步原函数读两次、候选一次，QPC/精度窗口/锚顺序保持指定顺序，返回值与冻结原函数逐次一致。
- `fallback`：QPC 不可用时每次一次 UTC，不进入 QPC 或精度窗口；首次 availability 查询的顺序变化显式断言。
- `backwards`：超过两秒的 QPC 倒退仍使用在用函数既有 0.0001ms guard，不套用旧函数的两秒限制。
- `tick-wrap`：GetTickCount 跨 DWORD wrap 的 32ms 间隔不会误触发 legacy drift flag。
- `drift`：超过 500ms 的 QPC/tick 差值仅清除 legacy flag，在用函数保持自己的同步锚；实际旧函数随后消耗该 flag 并重开配对精度窗口。

下面是当前候选的GREEN复现入口。需要CMake 3.24、Windows SDK、MSVC兼容编译器和PowerShell；Ninja方式须先进入相应x86/x64开发环境。以下x64 VS示例不改变项目默认工具链；x86将目录改为 `out/current-time-hot-path-x86`，`-A x64` 改为 `-A Win32`。人工FPS采样期间暂停编译：

```powershell
cmake -S tests/native/current_time_hot_path -B out/current-time-hot-path-x64 -G "Visual Studio 17 2022" -A x64
cmake --build out/current-time-hot-path-x64 --config Release
ctest --test-dir out/current-time-hot-path-x64 -C Release --output-on-failure
```

本次已在修改生产文件前保存两架构实际RED，再执行候选GREEN；不要把当前候选当作原始RED基线。

如需重新审查RED，使用单独固定在a00652824的源码根，在新的build目录配置本夹具并显式传 `-DSOUI_DISTRIBUTION_ROOT=PRIOR_A006_SDK`；不要重置或覆盖当前工作树。只运行 `ctest --test-dir NEW_BASELINE_BUILD -C Release -R '^current-time\.hot-path\.steady$' --output-on-failure`，其明确诊断应为 `candidate stable-QPC UTC calls: expected 0, got 4`。baseline的fallback尚保留旧首查顺序，其他候选断言由GREEN验证。失败不使用CTest `WILL_FAIL`倒置，不弱化断言。

这是调用冗余与行为边界验证，不是 benchmark，不产生 FPS、真实系统时钟精度或浏览器性能结论；内核和真实消费者仍需分别重编及验证。根目录已在可选 `SOUI_BUILD_VENDOR_TESTS` 组注册此目录，不改变默认组选项；也可使用上述独立入口。
