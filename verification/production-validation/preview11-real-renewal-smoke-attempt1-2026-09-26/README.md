# preview.11 真实续期夹具：x86 smoke attempt 1 失败原样本

本次实际启动了 x86 GUI 夹具，原生 PID 66336，计划时长 30,000 ms；在原生日志 elapsed 7,431 ms 因 `Unexpected document session shutdown or navigation` 失败，7,944 ms 记录最终退出码 **1**。`readyCount=0`、两类任务调用各 0、`shutdownSessions=1`，没有成功的 `probe.describe` 或 Web record。不能认定页面已就绪，也不能称为短 smoke 或真实续期通过。**本轮 x64 未执行。**

监督记录为非超时、未发现外部负载、结束时无剩余桌面进程，payload 与输入未变；这些只描述运行环境和收尾，不改变失败结论。Host destroy 与 runtime shutdown 返回成功。两个资源样本只作原始诊断，不用于资源或性能门槛。

## 原始核验

`raw/` 保存本次运行目录的全部文件原字节，包括监督器、冻结分析器、输入检查、门禁、载荷身份、NDJSON、空 stdout/stderr 和导航诊断。`raw-manifest.json` 逐项记录原路径、大小和 SHA-256。`Archive-Failure.cjs` 的 verify 模式仅调用冻结的 JavaScript 分析器，不启动原生程序、构建或 GUI；archive 模式拒绝覆盖并逐文件核对复制字节。

冻结 `Verify-Run.cjs` 已对实际 NDJSON 与 supervisor 执行一次，原始 stdout/stderr、完整绝对参数、时间和**数值退出码 1**见 `raw/verification.result.json`。分析器拒绝此失败样本，未产生通过 summary；原始证据和分析器散列未变。此核验是失败判定，不是再次运行 smoke。

## 诊断与尚未证实之处

冻结夹具 `session_shutdown` 在尚未绑定 probe 授权会话时也把所有非 closing 的通知判为失败。SDK Host 的已冻结源码显示：start 会放入一个 Host 生命周期 ID，主 frame 的初次脚本 context 创建可取走该 ID 并转发到同一 shutdown 回调。这是确定的夹具与生命周期通知语义不匹配路径。

实际导航诊断仅记录一条允许访问本地 index.html 的 URL，没有时间戳、收到的 session ID 或 context 来源；因此“本次正是初始生命周期 ID 退役触发”仍是与源码和时序一致的解释，**不能宣布精确运行原因已证实，更不能先归因 SDK 续期故障**。缺少 describe 也不能单独证明 bootstrap 从未成功。详细源码散列、行号、摘录及边界见 `raw/diagnosis.json`。

建议仅在新的夹具版本中增加无 token 的回调阶段/身份观察，区分 bootstrap 前的生命周期退役与实际授权会话关闭；在唯一成功 bootstrap 时立即绑定真实会话，确保 describe/ready 之前的已绑定会话丢失仍失败。ready 后导航禁止、一次 bootstrap、任务/订阅身份连续性和最终清理要求均保持严格。此次未修源码、未重新编译、未重跑桌面。

## 身份及结论

- SDK 输入 SHA-256：`a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550`。
- freeze SHA-256：`7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d`。
- native.ndjson SHA-256：`215d46601c76a042e0d6798f93547cd37f8463c7588db030b41d3deb231b8eb2`。
- 编译归档保留其历史时点；本目录补充后续真实启动失败，不覆写此前记录。
- `smokePassed`、`realRenewalVerified`、`resourceGate`、`benchmarkValid`、`releaseReady` 全部为 false；本样本不证明 35 分钟、8 小时、Win7 或生产能力。
