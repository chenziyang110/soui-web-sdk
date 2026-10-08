# Win7 V11 宿主采样覆盖率静态检查

V11 传输尚未创建或冻结，Win7 客体尚未启动。本轮在原有退出后终态采样之外，要求校验器确认首个宿主负载样本在启动后 15 秒内，且相邻样本时间单调、间隔不超过 15 秒。它只能发现采样缺口，五秒轮询仍可能漏掉两个样本之间完整启动并退出的短进程。不能据此认定客体隔离、缩放、中文 IME、任务并行或生产通过。

外部控制文件位于 `C:\SOUI-SDK-production-validation\work-area-placement-20260928`，本目录逐字节复制当前 `Review-V11.cjs`、`Test-Review-V11-TerminalGate.cjs` 和 `V11-PREPARATION.md`。上一轮控制文件的独立快照保留在外部 `v11-host-terminal-attempt1/control-originals`，不覆盖旧原始测试输出。当前校验器 SHA-256 为 `1c1dea6ce63837e58264e99ffe1cad5895e14691ee6277f4261acffceb0d015d`。

原始尝试各有独立退出码、stdout、stderr：语法检查 `v11-coverage-syntax-attempt1` 退出 0；首次测试 `v11-coverage-tests-attempt1` 退出 1，8 项中 7 项通过，失败因旧测试夹具预期了终态断言消息，实际先被新采样间隔断言拒绝；修正测试夹具后 `v11-coverage-tests-attempt2` 退出 0，8/8 通过，stderr 为空。失败原件未覆盖。三个尝试仅执行校验器语法和合成断言，没有启动 QEMU 或应用。

运行 `node verification/production-validation/v11-host-coverage-preparation-2026-09-29/Verify-Index.cjs` 对比外部原件、本目录副本与 Git 暂存区。V11 真正执行仍需宿主空闲后建立新冻结输入、确认真实 Win7 桌面、只发送一次命令，核对串口、原生退出、截图、身份及前后文件。`win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。
