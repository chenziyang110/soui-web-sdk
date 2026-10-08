# Win7 V11 宿主终态门禁准备

V11 客体传输尚未创建或冻结，也未启动 Win7。本归档只保存驱动、只读校验器和无客体的控制测试；它不证明窗口缩放、中文 IME、并行任务、资源性能或生产能力。

旧驱动只在 QEMU 运行中每五秒检查外部负载；历史 V02 客体已有外部 QEMU 在末次采样后、我方客体退出前出现的原始记录。V11 现于 QEMU 退出后再做一次 CIM 查询，记录 `qemuExitedAt` 和带 `after-qemu-exit` 阶段的终态样本。校验器要求该样本在退出后 15 秒内、无外部进程且无查询错误。晚出现的外部负载会保守地使本次隔离判定失败。

外部控制文件的本轮逐字节快照位于 `C:\SOUI-SDK-production-validation\work-area-placement-20260928\v11-host-terminal-attempt1\control-originals`；工作入口随后为下一轮覆盖率检查继续演进，本轮快照和八份原始测试文件保持不变。本目录逐字节保留 `Launch-V11.ps1`、`Run-V11.ps1`、`Review-V11.cjs`、`Test-Review-V11-TerminalGate.cjs`、`V11-PREPARATION.md` 和八份原始测试文件。测试结果：PowerShell 解析退出 0；JavaScript 语法检查退出 0；实际校验器断言块的六个正反例 6/6、退出 0，stderr 为空。驱动 SHA-256 为 `f3eed8598f68a49d22f9d231ef353e0af9cc81c2491d3cfc8300c64632fff065`，校验器为 `1f93c2c7769243755521f1b9c7debbabd43303f7dd8a28f5bd71114dc8e2294a`。

运行 `node verification/production-validation/v11-host-terminal-preparation-2026-09-29/Verify-Index.cjs` 可比对外部原件、工作区和 Git 暂存区。宿主空闲后，仍需新建 V11 传输、冻结并验证、确认真实 Win7 桌面、只发一次客体命令，再保存退出、串口、截图、身份与前后载荷证据。当前 `win7CurrentV02Verified=false`、`benchmarkValid=false`、`releaseReady=false`。
