# preview.15 Win7 SP1 x86 三入口短测

本次通过的是冻结的 preview.15 原仓库 x86 Release 应用功能短测。Starter、Workbench 默认页和 React 页均在原 60 秒期限内正常退出，未超时、未遗留对应进程。默认与 React Workbench 均完成页面/Bridge 就绪、适配器卸载、Bridge 关闭、事件排空、WebView 销毁、宿主关闭及窗口销毁。

| 项目 | 已执行结果 |
|---|---|
| 客体身份 | Windows 7 企业版，6.1.7601，SP1，32-bit，指针 4 字节，PowerShell 2 |
| Starter | 原生退出 0；`application-ready` → `host-close-complete` |
| Workbench 默认页 | 原生退出 0；完整卸载、关闭和窗口销毁顺序 |
| Workbench React 页 | 原生退出 0；完整卸载、关闭和窗口销毁顺序 |
| 输入和运行载荷 | 传输文件、ZIP 测试/解压、运行前后完整载荷及 resources/legal 校验通过 |
| 宿主隔离 | 启动前门禁、63 次运行采样均未发现其他桌面/编译/QEMU 负载 |
| 宿主退出 | 串口完整导出后关闭自身 QEMU；QEMU 与外层驱动均原始退出 0 |

输入沿用[已提交的当前 v3 冻结](../win7-sp1-x86-qemu-preview15-apps-v3-preparation-2026-09-27/README.md)，没有修改应用或测试期限：

- 版本 `0.3.0-preview.15`，原仓库构建 ID `b8c26d8d-7144-4eb0-b889-0034ec8029fb`。
- 717 文件构建源码 SHA `b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07`。
- 612 文件冻结 SHA `47e463d7746dd9824a6083d3e12b26d0910ac79a35bd7d69060b1ffe62b9e005`；冻结输入与基础盘前后核验一致。
- 原串口 179,152 字节、37 个文件帧，SHA `f5e612f12c5994304d45b286461d5d7800f8dd9dd6fe5cc3157d86fc8307fe22`。冻结的只读严格校验器和宿主复核均原始退出 0。

宿主 UTC 2026-09-26 18:22:59 启动，18:24:15 在观察到桌面和 D: 传输盘后发送唯一固定命令，18:28:40 完成退出与后置核验。客体日志的 UTC 时间与宿主相差八小时，原件保留；不跨时钟推算启动/响应时延，也不将日志中的绘制间隔当作物理帧率证据。

89 份原始记录由 `raw-manifest.json` 索引，包含完整串口、只读校验器实际命令、37 个逐帧解码文件、完整门禁和前后散列、发送记录及桌面截图。解码是另行读取串口并写入新目录，严格校验器不修改原串口或旧样本。此处只保留短测结论，原 SDK 包和源码 ZIP 均未替换。

此前两次尝试分别保留：

- [attempt1](../win7-sp1-x86-qemu-preview15-apps-attempt1-2026-09-27/README.md) 在命令发送前被外部 QEMU 干扰，保护性停止自身客体。
- 本目录 `raw/attempt2` 与对应命令日志记录宿主模块路径导致 `Get-FileHash` 不可用、驱动退出 1；没有启动 QEMU 或发送客体命令。新的 `Run-Guest.ps1` 只为自身进程指定 Windows PowerShell 系统模块路径，确认命令可用后调用原 `Launch-Current.ps1`，客体冻结内容不变。

旧 preview.11 的 Workbench 关闭停滞没有在本次当前输入中重现；一次短测不能关闭长期/重复关闭稳定性门槛。仍待 V02 真实滚动、中文 IME、DPI 与公共任务并行，驻留/多窗口/生命周期循环、真实续期、隔离八小时、物理目标机与性能对标。`sourceReproductionClaim=false`、`realRenewalVerified=false`、`releaseReady=false`。
