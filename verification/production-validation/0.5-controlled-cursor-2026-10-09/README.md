# 0.5 受控交互切片的有限验证记录

这些记录是源码和产物身份绑定的诊断结果，未分配新的发布包身份。被测代码当时尚未提交；其中的基线提交由 Root 读取确认，实际修改以逐文件散列表示。

| 记录 | 实际执行内容 | 范围 |
|---|---|---|
| [SDK 与主上下文门](sdk-static-and-gate.json) | 完整 SDK 的 22 个 C++ 文件编译；辅助程序的 14 个内部用例直接执行，再由 1 个注册 CTest 执行同样 14 个用例 | Host 编译；未执行 Host 的 opaque getter、开启 Test Kit 的配置或内核 DLL |
| [首轮 vendor 编译](vendor-selected-compilation.json) | 12 个实际 vendor 文件和新建的实际 config PCH | 历史 `wke.cpp` 身份；该文件之后已修改，不能追认当前完整内核 |
| [生命周期故障回归](lifetime-fault-regressions.json) | 各 9 个候选正例、6 个旧版真实非零反例、3 个旧版对照、9 项候选 CTest；随后编译实际修改的 2 个完整 vendor 文件 | 端点算法使用精确源码片段及叶子类型替身；Activation 使用实际源码和真实 Win32 线程/临界区。实际完整文件的编译使用实验 GPU 配置，仅作类型与异常展开检查 |
| [公共 portable 回归](portable-lifetime-suite.json) | x64/Win32 各 9 项直接执行和 9 项 CTest；各 3 个实际辅助程序对象、2 个辅助程序 | 生成的 3,994 字节片段与源码逐字节相等；Activation 编译实际完整文件。未执行完整 Blink view 或内核 DLL |
| [x64 默认软件内核](vendor-full-software-x64.json) | 实际 3,025 个 C++ 文件编译并链接完整 DLL，另有 1 个汇编对象；核验架构、导入和 59 个必要导出 | 使用 11 个历史核心库，其当前源码来源尚未闭合；编译监督 generic false 保留。此记录不证明运行或关闭成功 |
| [普通 SDK 关闭失败](normal-sdk-native-close-failure-x64.json) | 实际普通 SDK 与新 DLL 动态接入；未初始化 ABI 进程真实卸载成功；Host 启动、真实子窗口和主文档就绪后，关闭 90 秒超时 | 812 次关闭 pending，实际 root/supervisor 124，进程组已闭合。Host 销毁、运行时关闭和初始化后的模块状态均未得到成功回执 |

所有原始失败状态都保留：编译阶段的 root 0 / supervisor 124 / generic false，旧版反例的实际非零退出，普通 SDK 的实际关闭超时，以及最初 `/Fp` 解析器误认 `/fp:precise` 的核验失败。自有进程组的闭合不把这些 generic false 变成成功。首轮三份记录包含 95 个原始 Job 报告散列，补充三份记录另包含 40 个实际阶段的报告散列；不包含工具、DLL、系统文件或私有源快照的正文。

源码集合的前后比较、事后读取散列与完整编译输入绑定分别记录。事后散列不能补成启动前冻结，`actualCompilerInputBindingComplete` 保持 false。SDK 父测试入口和 vendor `wke.cpp` 的后续变化有明确历史标记。

公共 portable 的有限回归及 x64 完整软件 DLL 编译已实际执行；本批普通 SDK 诊断的启动和主文档就绪也有实际回执，关闭失败。这些历史记录不证明完整构造回滚、worker 退出、完整 GUI、目标系统、性能、资源收敛或发布资格。`validationPassed`、`qualificationPassed` 和 `releaseReady` 均保持 false。

公共片段生成 V1/V2/V3 的实际换行失败仍保留。V4 明确要求无 NUL、LF 换行且以 LF 结尾，拒绝其他输入；这不把原先无末尾 LF 的生成要求追认为通过。构建前继续核验整份源码摘要和生成片段的 HEX。

公开三份记录的独立转换审查 proof SHA256：`d84aa6879f0b1092a4d53775d1886e59c4e1e0d8274d06da928995617390bbcc`。它只核验元数据与原始诊断的对应关系，不新增原生验证结论。

补充三份记录的独立转换审查 proof SHA256：`1a8b17fe3c2ab8da0bbc3b75520869264a96d939f778d625469983df4d393289`。普通 SDK 的失败与初始化模块状态未知均按原始结果保留。

后续重复关闭修复的[双架构叶回归](../0.5-request-close-2026-10-09/README.md)和[新 DLL、普通 SDK 关闭及 56 项原生测试](../0.5-native-finite-2026-10-09/README.md)另行归档。后续普通 SDK 关闭成功绑定不同 DLL，不能修改或追认本目录的原始失败。
