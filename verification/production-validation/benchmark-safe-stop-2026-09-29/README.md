# 性能监督器失败清理边界

本记录只验证基准监督器的失败清理行为，不是隔离性能样本。执行时宿主有其他 QEMU；没有运行正式 SOUI/Electron 配对，`benchmarkValid=false`、`releaseReady=false`。

Windows 的进程 ID 会复用，`ParentProcessId` 可能错误地指向后来占用该 ID 的进程（[Microsoft 进程事件文档](https://learn.microsoft.com/en-us/windows/win32/etw/process-v2-typegroup1)）。`taskkill /T` 的公开契约是终止指定进程及其子进程（[Microsoft taskkill 文档](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/taskkill)）；它没有提供本轮每个后代的创建身份校验。因此，直接子进程的身份核验不足以授权监督器在失败时使用 `/T`。这是一项保守的清理边界，尚未以真实复用 PID 的进程树证明 `taskkill` 会误杀。

`run-one.cjs` 和 `run-series.cjs` 现在只通过各自持有的 `ChildProcess` 停止直接子进程，明确记录后代清理未证实。下一轮正式运行前必须检查残留；当前代码不会把“已请求停止直接子进程”记作“整树已清理”。未来若需要可靠的整树强制清理，应引入能持有并绑定后代身份的机制，另行验证。

原始测试输出不可覆盖：

| 尝试 | 原始退出 | 实际结果 |
| --- | ---: | --- |
| `red-attempt1` | 1 | 新边界的两项测试均失败；旧实现仍使用整树终止。 |
| `green-attempt1` | 1 | 22 项中 21 项通过；夹具误以为停止父进程后后代必然存活，该假设在此环境不成立。 |
| `green-attempt2` | 0 | 修正该夹具假设后，22/22 无窗口测试通过，stderr 为空。 |

每次的 stdout、stderr 和实际退出码均保留为独立文件。`Verify-Index.cjs` 逐字节比较外部原件、工作区与 Git 暂存区，并检查失败/通过计数。这个功能回归不能关闭五对真实性能、物理呈现、真实中文 IME 或生产门槛。合并分支的完整源码清单还须在宿主空闲后针对最终暂存输入刷新、验证，才能提交。
