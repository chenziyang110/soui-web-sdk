# preview.15 Win7 第一次客体尝试：命令发送前受外部负载中断

本次不是 SDK 应用通过或失败样本。Win7 已显示桌面，但测试命令尚未发送，另一个任务的 QEMU 启动后，隔离保护停止了本次客体。没有终止其他任务。

输入沿用[已冻结的 v3 当前应用输入](../win7-sp1-x86-qemu-preview15-apps-v3-preparation-2026-09-27/README.md)：612 文件，freeze SHA `47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005`；原仓库 x86 Release 构建 `b8c26d8d-7144-4eb0-b889-0034ec8029fb`。没有更改应用、输入、60 秒单项期限或 720 秒客体总期限。

| 记录（UTC） | 事实 |
|---|---|
| 2026-09-26 18:07:35 | 启动自身 QEMU PID 83580；启动前二次门禁为空 |
| 18:09:40 | 截图显示 Win7 桌面、任务栏和 QEMU 磁盘驱动通知 |
| 18:10:17 | 外部 `qemu-system-x86_64.exe` PID 28876 启动 |
| 18:10:23 | 运行门禁捕获外部 QEMU，驱动停止自己持有的客体进程 |
| 18:10:29 | 驱动终态 1；客体进程退出 -1，非超时；基础盘与冻结输入复核完成 |

`command-send.json` 不存在，原始串口文件为 0 字节；没有执行三个 SDK smoke，也没有 guest identity 或应用资源前后导出。截图仅证明本次可观察的桌面画面，不替代客体内系统身份和 SDK 验收。

基础盘前后 SHA 均为 `fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4`。冻结校验前后通过；`qemu-result.json` 保留 `externalLoadObserved=true`、`timedOut=false`、`gateFailure=null`。退出 -1 是保护性终止后的 QEMU 进程状态，不是 SDK 原生退出码。

36 份原始记录包括前后门禁、全部期间采样、原串口、截图及原 PPM、驱动原始输出、实际 `Launch-Current.ps1` 与保留的 `Launch.ps1`。两份临时 `vl.*` 快照继续留在外部原目录，只归档大小和散列，不提交磁盘快照。新驱动增加紧邻启动的第二次门禁和每五秒采样；门禁查询失败也停止自身客体。

归档复核第一次因将 Windows PowerShell 重定向的 UTF-16LE 日志按 UTF-8 解码而退出 1，旧辅助脚本与观察说明保留。修正为按 BOM 解码后，复核原始退出 0；未改任何原始日志字节。该错误属于宿主归档辅助工具，不是 SDK 或 Win7 失败。

下次须等待所有其他桌面/虚拟机/编译负载退出，再创建新的 attempt2，复核相同冻结输入，确认实时桌面，绑定截图后仅发送一次原固定命令。保留本次原件，不自动放宽期限。`guestApplicationExecuted=false`、`win7ApplicationPass=false`、`releaseReady=false`。
