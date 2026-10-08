# V8 Win7 V02 输入：编译与冻结完成，客体启动前保护性拒绝

本次仍绑定 preview.15 Demo 的 718 文件源码 SHA-256 `15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d` 和正式 x86 Release buildId `f0c38056-058a-456e-be99-17446127f7ec`。V8 为 [V7 真实缩放失败](../win7-sp1-x86-v02-interaction-v7-resize-blocked-2026-09-28/README.md)后的独立探索输入：先用自然前台鼠标将窗口拖进工作区，再尝试原有滚轮、缩放、任务和中文输入。它不是应用默认窗口位置修复的验证。

`probe-v8-build-attempt1` 的 x86 配置和编译原始退出码均为 0，静态运行库检查通过；原生输入探针 EXE SHA-256 为 `2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96`。`layout-interaction-v8-attempt1` 的准备、冻结、验证原始退出码均为 0，冻结文件 SHA-256 为 `764452636fd31dd5cac25611882f67fe578a7fe8357e36ce9d79a9826166502b`。冻结输入预计 40 个动作、118 个串口帧；这只是验收预期，尚无客体结果。

`guest-layout-interaction-v8-attempt1` 启动前第二次门禁于 `2026-09-28T05:14:20Z` 发现外部 QEMU 进程。驱动原始退出码 1；本次没有启动自己的虚拟机，没有发送客体命令，也没有串口或 GUI 通过结论。门禁记录、完整标准输出和错误输出均作为失败原样保留。后续只能用全新 guest attempt 名和相同冻结输入，在宿主确实空闲、确认 Win7 桌面后运行一次。

[53 份精选原件及逐文件散列](raw-manifest.json)包括输入源、实际编译日志和 CL/link 命令、EXE、冻结元数据、命令原始退出码、二次门禁及保护性拒绝日志。仓库没有复制两份大型应用 ZIP 或虚拟磁盘；完整冻结传输仍在 `C:\SOUI-SDK-production-validation\preview15-v02-win7-current-20260927\layout-interaction-v8-attempt1`。运行 `node Archive.cjs verify` 可复核仓库副本的散列、存在时的外部原件，以及编译成功、冻结身份和启动前拒绝。此记录不证明真实中文 IME 组合、公共任务启动、滚动缩放完成或生产能力。
