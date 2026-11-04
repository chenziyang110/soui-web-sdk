# ProcessTreeSampler 的 Toolhelp 完整性合同

fixture 直接 include 当前生产 `process-tree-sampler.cpp`，只以宏排除程序 `wmain`；生产枚举模板接受 fake Win32 API。没有重写枚举算法，没有新 SDK API/头或 measurement-validation SourceFiles 项。

13 组合同覆盖 Next 正常18/中途错误/错误0、First错误/错误0/空18、无效快照、Close错误/错误0、双错误保留、visitor异常时关闭，以及生产 collect/encode 的 partial tree 丢弃和 scope。正常返回在 `return` 前显式 close，不依赖 NRVO；Clang MSVC frontend 使用 `/clang:-fno-elide-constructors`，GNU/Clang GNU frontend 使用 `-fno-elide-constructors` 禁用可选复制省略。必须核实际编译参数及无 ignored/unknown 参数诊断，不能凭参数文本出现就声称该覆盖已生效。成功路径只访问 fake entries；partial/empty 的 collect 早退，没有真实进程树采样、GUI、输入、计时或内存性能测量。

从仓库根目录的独立入口（具体执行由维护者选择已验证工具链；此文不表示已运行）：

```powershell
cmake -S tests/benchmark/process-tree-enumeration -B out/process-tree-enumeration -A x64 -DCMAKE_SYSTEM_VERSION=10.0.19041.0
cmake --build out/process-tree-enumeration --config Release
ctest --test-dir out/process-tree-enumeration -C Release --output-on-failure
```

枚举只在 false + `ERROR_NO_MORE_FILES` 时认为耗尽；其他 false（即便错误0）失败。先保存原错误，再关闭快照；Close失败单独保存且不覆盖先前First/Next错误。close恰尝试一次，异常不替换原visitor异常。`collect` 不以截断entries建立父子树；实际run保留不完整样本并立即结束非零，根进程同时退出也不能掩盖枚举失败。输出失败仍按原逻辑非零，不能凭缺记录宣称完整。

`complete=true` 仅表示这一轮已枚举到的进程及原有PID/creation校验读数完整。短命进程可能完整地存在于两次轮询之间；未观察的共享浏览器、已存在的外部进程不因页面使用它就归属该树。样本明确 `completeScope=observed-processes-at-poll`、`allLifetimeProcessesObserved=false`、`sharedBrowserAttributionVerified=false`。不将零枚举错误改称完整生命周期、共享进程归属、无泄漏或完整资源覆盖；正常PID/creation与已知后代保留规则未改。

失败样本的 `enumeration` 保存 `complete/failure/win32Error/closeFailed/closeError`；即便失败API返回错误0，独立布尔值仍保留Close失败。部分列表不被输出为有效成员。空快照18可以是成功枚举，但缺少仍活跃root时样本仍complete=false。原v2必需字段不删，现有reader仍拒绝completefalse/incompleteSamples；新增字段不能覆盖其拒绝。

Win32依据：[Process32NextW](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-process32nextw)、[Process32FirstW](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-process32firstw)、[CreateToolhelp32Snapshot](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot)、[CloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle)。
