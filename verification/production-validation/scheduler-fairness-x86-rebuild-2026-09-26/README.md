# 调度候选 x86 完整重编与原动画实测

源码候选为 `2bf961be7b120b56c286dccdc983ed6c91b25e0e`，关联 [PR #36](https://github.com/chenziyang110/soui-web-sdk/pull/36)、[原生失败证据](../preview11-renewal-scheduler-stack-2026-09-26/README.md)和[策略红绿回归](../scheduler-fairness-policy-2026-09-26/README.md)。本轮证明 x86 重编内核消除了这个冻结页面的启动失败，尚未证明完整 SDK 包、其他平台或生产能力。

## 完整底层构建

在新输出目录实际执行 `powershell.exe -NoProfile -File tools/Build-Dependencies.ps1 -Architecture x86 -Component All`，包含十一项依赖库、Skia GPU、Miniblink 和五项 SOUI 组件。构建从 2026-09-26 14:55:14Z 到 15:43:33Z，原始退出 0、没有外层超时。28,163 个源码/构建辅助输入的冻结 SHA-256 为 `012bfc5c482f59dc14452a76cc13e7536662950e218ada4e888ad475a8d7d652`，结束后逐项复核不变。

六份产物的文件散列和 x86 PE 身份通过复核。新 `node.dll` 为 22,988,288 字节、SHA-256 `2ed793ccccb0642df9916128e36aece733efd34ad6acb4e50caa0622a4c25ef3`。十八份完整构建日志和三十三份实际编译/链接命令已保存；命令未发现外部 vcpkg 路径，未发现 C4819/C4828 源码编码警告。其他第三方警告仍保留，包括 C4789；尚未完成逐项风险排除，退出 0 不表示内核安全门通过。现有固定预编译文件逐项核验未变。

## 原动画真实运行

以修正监督器计时竞态后的 preview.11 独立消费者为父输入，在新的七十文件载荷中只替换上述 `node.dll`；EXE、页面、JS、其余运行库和原动画均不变。新冻结 SHA-256 为 `ef2767b3f0d853c8ae509454e64fb55aa0e157eb7b890b32dcff7217477e9fc2`。未附加调试器，初始 60 秒期限、30 秒任务时长及关闭规则保持原样。

其他桌面和编译负载退出后运行，原生在约 1.1 秒记录 bootstrap，公共长任务和结果任务各调用一次，返回结果 42、完成取消及退订，宿主与运行时退出。76 条连续原生事件的末条为 34,107 ms、退出 0，ready/bootstrap 各一次。原始退出码、所有启动/期间/结束门禁、完整载荷前后散列已独立复核，未观察到外部负载。此时间只描述这一短测，不是性能基准或 50 ms 响应保证。

## 范围与后续

[91 份原始记录](raw-manifest.json)包含构建输入、产物身份、完整日志/命令、新消费者冻结、运行记录和[复核汇总](raw/driver/dependency-review.json)。二进制、PDB 和大型构建中间物保留在外部验证目录，不重复提交。

它是“旧消费者 + 新内核”的定位回归，不是当前版本 SDK 整包通过；本轮没有覆盖新编译 SOUI DLL 的实际应用运行。x64 完整重编随后开始；两架构 `Test-Dependencies.ps1`、当前 Starter/Workbench、Win7、V02、真实超过 30 分钟续期、长稳资源及全部生产门槛仍需执行。固定 DLL 和候选版本尚未替换，实际进入交付前仍须升级版本并刷新依赖身份。

`currentSdkPackageValidated=false`、`realRenewalVerified=false`、`productionPass=false`。
