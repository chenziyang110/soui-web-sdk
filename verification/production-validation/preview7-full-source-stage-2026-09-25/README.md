# preview.7 无 Git 完整源码快照的双架构应用重建

`source-archive.cjs` 从暂存区生成 preview.7 的完整源码清单，`verify` 通过后，在仓库外 `D:\SOUI-SDK-QEMU-validation\preview7-full-source-stage-20260925` 建立无 `.git` 快照。该次快照清单 SHA-256 为 `0EDD1D2644B17FEECD942A0EB592D523698B9A692CD96AD82217C171D3468B47`，包含 30,712 个文件；创建、编译完成后各自再次 `verify` 通过。运行前 `Doctor.ps1` 检查 Node、CMake、MSVC v143、SDK 19041、ATL、Miniblink 与 SOUI 源码均可用。

同一份快照分别执行 x86/x64 Release `Build.ps1` 和完整 `Test.ps1`，所有命令退出 0。x86 构建 ID `d63ce8ab-acba-46b4-a52a-86d77dd6dfa0`，x64 构建 ID `41d261ea-3057-4542-8fb1-d2218bc4b12f`；两者构建源码指纹同为 `7b3813eb4516f4e4870040840343cbe8a661c59a744853d4e5cd32f1b2af419a`。原始[构建报告 x86](x86-build-Release.json)、[x64](x64-build-Release.json)和[三入口 smoke x86](x86-smoke-Release.json)、[x64](x64-smoke-Release.json)保留完整载荷身份。两架构[测试日志 x86](x86-test.log)、[x64](x64-test.log)均记录 SDK 包 14/14、Web/监督器测试 58 通过和 1 项因未配置 x86 调试器跳过、原生 CTest 6/6、Starter/Workbench 默认页/React 页 smoke 3/3。

这是应用与固定预编译运行依赖的完整源码搬移重编，和[修改过的 preview.6 目录里重编底层依赖](../preview6-relocated-dependency-encoding-2026-09-25/README.md)分别统计。添加本证据后清单会重新冻结；代码未因此修改。最终 preview.7 原封完整源码包尚未在全新目录从零完成 **底层依赖** 双架构重编，Debug、长期资源、物理 Win7 x86 与授权核查也未关闭。因此 `sourceDeliveryVerified=false`、`releaseReady=false`。
