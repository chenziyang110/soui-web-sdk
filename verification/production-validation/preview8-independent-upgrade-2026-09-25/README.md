# preview.7 → preview.8 独立业务项目升级、回退与再升级

状态：**本机 x86/x64 Release 四阶段均通过；生产准入未通过**。从 preview.7 的无 Git 完整源码新建仓库外 `UpgradeProbe`，在应用源码加入独立业务头文件和可从真实启动日志核对的 `business-marker=customer-state-2026`。旧、新 SDK 包均先通过包内完整性校验；preview.8 包锁清单 SHA-256 为 `fd3e4b3514b41c02be39f596a67a0be9ee6311240a325c5a99a22d6195cb03a9`。

按[实际执行脚本](procedure-as-run.ps1)依次完成 preview.7 初始、升级 preview.8、回退 preview.7、再升级 preview.8。每个阶段均校验 SDK 锁，分别重新编译 x86/x64 Release，在真实应用中运行 `--smoke-test`，检查退出码、业务标记、`application-ready`、`host-close-complete`，并核对构建 ID、源码指纹与完整运行载荷；两个[业务文件原件](business-source/app/)在所有安装操作前后 SHA-256 保持不变。[阶段报告](result.json)记录四阶段与 8 个不同构建 ID，[独立核验结果](verification.json)逐项复核 58 份原始文件；[核验器](Verify-Upgrade.cjs)的篡改日志[反例自检](self-test-output.txt)退出非零。

本次只证明该业务小样在当前 Windows 11 宿主的双架构 Release 构建、启动及升级回退链。未覆盖安装中断、受限账户、签名、Debug、物理 Windows 7 SP1 x86 或长期运行，不能据此设置 `sourceDeliveryVerified` 或 `releaseReady` 为 true。
