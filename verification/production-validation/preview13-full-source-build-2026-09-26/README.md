# preview.13 完整源码解压与四配置构建

本轮从修正许可证身份后的完整源码 ZIP 解压到新的无 Git 目录，完成 SDK、Starter、Workbench 的 x86/x64 × Debug/Release 构建。四次构建原始退出码均为 0；随后四组各 11 项原生 CTest、解压目录内 8 项交付身份测试通过。未运行这些新载荷的 GUI。

## 输入与边界

- 版本：`0.3.0-preview.13`，源码提交 `bd5ee5c956ae8352d0872c07dc0f9d3f3c828141` 的许可证修正及回归。
- 完整源码包：`soui-sdk-0.3.0-preview.13-legal-identity-full-source.zip`，SHA-256 `bbd0aa35d66ae85662fff9e4047598f82507974fb9ee48bffd2c99d9d95de9fd`，33,200 文件（完整清单 33,199 项）。该 ZIP 在上述提交前按已暂存相同源码生成，后续证据文档提交不改变这一被实际构建的 ZIP 身份。
- 验证输入：714 文件，SHA-256 `a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68`。
- 解压目录与现有 419 文件 SDK 包正式 `verify-source` 匹配，退出 0；完整源码清单在构建和测试后再次通过。保留原许可证不匹配的失败 ZIP 与解压目录，见[先前失败](../preview13-legal-identity-2026-09-26/README.md)。
- 只在构建子进程中关闭 vcpkg 自动集成并设置兼容的 PowerShell 模块目录，未修改宿主全局配置。实际编译/链接记录无 vcpkg 路径。使用固定预编译底层依赖，未执行本候选全量底层重编，也不是干净机器复现。

## 实际结果

| 架构 / 配置 | 构建 ID | 原生测试 | 已保存运行载荷 |
|---|---|---|---|
| x86 Release | `ed7e823b-06bc-48b2-9e6e-dbefa0ee312c` | 11/11 | Starter 71 文件、Workbench 475 文件 |
| x64 Release | `899c8cd9-d030-4097-84c6-cfb7575e7b12` | 11/11 | Starter 71 文件、Workbench 475 文件 |
| x86 Debug | `729f55b2-e8a1-4bd5-949e-3492c0d03079` | 11/11 | Starter 71 文件、Workbench 475 文件 |
| x64 Debug | `b61c4068-dfa7-470f-8131-49c101e22bee` | 11/11 | Starter 71 文件、Workbench 475 文件 |

八份运行载荷逐文件复核，每份 60 个 EXE/DLL 的 PE 架构与目标一致。独立复核还检查四组构建 ID、SDK/Starter/Workbench 实际编译和链接记录、命令退出码及 stdout/stderr 散列。首次复核脚本只接受 TAP 的 `# pass` 文本，遇到 Node 实际 `ℹ pass` 格式而失败；保留脚本快照，第二次接受两种明确格式并完成全部复核。这不是重跑测试或放宽测试成功条件。

原生默认测试不执行真实剪贴板、窗口线程超时、目录取消或呈现页面；不能替代桌面往返验证。`result.json` 的 `unitTestsExecuted=false` 是构建完成时点，后续测试结果独立记录于 `test-result.json`。三项生产准入标志继续为 false。

## 原始记录

[原始文件清单](raw-manifest.json)收录 314 个文件，含打包、解压、四次构建、实际编译/链接记录、四次 CTest、交付身份测试与[独立复核](raw/source-build/independent-review.json)。运行二进制、完整源码及依赖缓存不重复提交；原件位置由清单逐项记录。外部源目录为 `preview13-full-source-build-attempt2-20260926`，运行副本位于各 `builds/build-<架构>-<配置>/<应用>`，下一次运行前必须按对应 `build.json` 再核验。

当前仍缺本候选 GUI、Win7 与 V02/V04、独立业务实际升级运行、底层完整重编、长期稳定、Electron 等价对照及发布责任。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
