# preview.11 外部续期监督器计时修正

本轮修复外部验证夹具的时钟/快照竞态，不修改 SDK 或冻结 v2 输入。原失败在[动画诊断](../preview11-renewal-animation-diagnostic-2026-09-26/README.md)保留：原生在页面记录后 15 ms 报“30 秒无记录”。

## 修正与验证

原轮询先读单调时钟，随后锁定并复制共享观察状态。回调可在两者之间写入较新的 `last_record_at` 或 `finish_at`；无符号相减下溢可能造成记录超时误报，或提前满足关闭宽限。新夹具把相同采样逻辑提取到实际轮询使用的 `sample_clock`，先取快照，再读单调时钟；不增加期限、不钳制差值、不忽略失败。

确定性交错测试在旧顺序下实际编译成功、运行退出 1，错误为 `Fresh callback falsely exceeded record gap after interleaving`。修正后 x86/x64 各 8 项检查通过，包括回调与时钟读取交错、30,000 ms 记录期限及 500 ms 关闭宽限两侧边界。两个实际消费者重编及原有 45 项阶段自检、8 条日志检查均退出 0。SDK 安装输入与父冻结匹配，实际编译/链接无 vcpkg 路径。

两架构分别复制父载荷的 70 个文件，只替换新编译的 `InstalledRenewalProbe.exe`；每份 60 个 PE 架构复核。新的外部冻结文件 SHA-256 为 `b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef`，schema 为 `soui.renewal-supervisor-clock/v1`。原消费者自检中的 `fixtureRevision=2` 是原阶段协议标记；本次额外身份由 `supervisorClockRevision=1` 与新冻结文件绑定，不能把它伪装成旧冻结 v2 的通过结果。

## 实际桌面短测

桌面空闲后串行运行 x64、x86，每次在新的运行副本中仅关闭原页面的一处旋转动画，以隔离尚未修复的加载问题。两个原生均退出 0，各 77 条连续日志，bootstrap/ready 各一次，两类公共任务执行、结果 42、取消、取消订阅及关闭完成。输入前后核验、命令输出散列和门禁复核通过，未见外部桌面负载。

这是监督器修正后的动画关闭诊断短测；原始动画开启时的启动问题尚未修复，30 秒没有跨过授权期限。真实 35 分钟/八小时续期、V02 动画并行、资源趋势和生产结论均不通过。

## 复核与复用

[122 个原始文件](raw-manifest.json)包含修改前源码、红/绿三份源码快照、实际编译命令、原始退出码、两架构运行日志及[复核报告](raw/review.json)。输入及运行二进制保留在外部 `preview11-renewal-supervisor-race-20260926`，未重复提交。旧父输入和所有失败样本保持原字节。

新 `Run.cjs` 只接受新冻结 SHA，强制校验两份载荷与控制文件、门禁，并写入新运行目录。它的用途固定为动画关闭的诊断短测；不要用旧 v2 `Run.ps1` 或分析器直接认可这些新 EXE，也不要把本轮重编消费者当作当前 preview.13 应用验收。后续应先完成动画开启条件下的调度诊断与修复，再整合正式长测输入和校验器。

`sdkSourceUnchanged=true`、`animatedStartupFixed=false`、`realRenewalVerified=false`、`productionPass=false`。
