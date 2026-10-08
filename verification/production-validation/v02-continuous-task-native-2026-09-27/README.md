# V02 持续任务：两架构原生回归与页面类型检查

当前 preview.15、`codex/sdk-v02-continuous-task` 的 Demo 输入为 718 文件、SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d`。原始命令、退出码、门禁、输入和失败样本见 [raw-manifest.json](raw-manifest.json)。此结果仅覆盖新 Demo 的无窗口原生回归与 Web 类型检查；正式应用重建、真实桌面与 Win7 x86 验收尚未执行。

## 实际结果

| 执行 | 结果 | 证据边界 |
|---|---|---|
| x64 attempt1 | 配置和两个测试目标编译退出 0；外部 QEMU 在测试前出现，驱动退出 1 | 两个测试均未启动；编译产物及 CL/link 命令已冻结，不算通过 |
| x64 attempt2 | 配置、编译退出 0；首个持续任务场景通过，随后测试退出 1 | 测试试图在同一适配器内第二次执行可信 `bridge.bootstrap`；原始失败、测试源码和可执行文件已冻结 |
| x64 attempt3 | 配置、编译、持续任务集成测试、既有 Demo 续期测试和驱动均退出 0 | 两项真实 adapter/Bridge 无窗口回归通过，前后源码相同、每步门禁空闲 |
| x86 attempt1 | 同样两项原生测试及驱动均退出 0 | 前后源码相同、每步门禁空闲 |
| Web attempt1 | Workbench Web TypeScript `--noEmit` 退出 0 | 类型检查，不等于生产 Web 构建或 GUI 验收 |

x64 attempt2 暴露的是新测试夹具与单可信 WebView bootstrap 契约不符。测试现为另一会话签发有效的 `task.query`/`task.cancel` 权限，再断言跨会话访问被任务所有权以 `E_TASK_NOT_FOUND` 拒绝；未改 SDK Core 或 Demo 运行实现。绿灯覆盖 1 秒真实单调时间、重复异步进度、运行中取消、非法输入、关会话清理、原快速任务，以及既有续期契约。原始失败没有覆写。

新 V02 页面尚无与此次源码匹配的 Release 应用构建身份，也没有滚动、真实中文 IME、缩放及任务并行的 GUI 记录。旧 preview.15 应用和 Win7 短测绑定变更前输入，不迁移为本次通过；`releaseReady` 仍为 false。
