# 当前 V02 操作系统输入探针双架构编译

状态：**x64/x86 辅助程序已编译，真实 GUI 输入未执行，生产验收未通过**。本辅助程序不进入 SDK 或 Demo 运行载荷，只为后续真实 V02 数据页验收生成操作系统输入证据。

`probe-build-queued-attempt1` 从 2026-09-27 10:07:20Z 起每约 5 秒查询外部 QEMU、SOUI、消费者、测试及编译进程。78 次原始门禁采样中，71 次检测到其他 QEMU 并持续等待；10:14:05Z 起观察到空闲，连续空闲后按 x64、x86 顺序从冻结的两份源码构建。四个 CMake 配置/编译阶段原始退出码均为 0，未记录阶段超时、门禁错误或外部负载；等待器于 10:14:33Z 以 `built-x64-x86` 结束，进程退出。构建阶段均短于 5 秒，中途没有独立采样，因此这些记录不能提供隔离资源或时延结论。

| 架构 | `V02InputProbe.exe` 大小 | SHA-256 |
| --- | ---: | --- |
| x64 | 47,104 字节 | `f58219498883c31729414d51a48a3ec89cd4a5105d4e5f5b39c7919fefd664c7` |
| x86 | 41,472 字节 | `3ae9a6e42fa2d7980d031cf2dccf62a9245952089a54dae215bcacaadf95d5ef` |

冻结的 `V02InputProbe.cpp` SHA-256 为 `a74b82a53197436c1678d207a078f94ba55dd31f1cb2f752cce75888152f4c08`，`CMakeLists.txt` 为 `78d424a27cc8f949602950400c382adc04ae49f395ebb275384e82bc01ce46d9`。原始源码、驱动、完整门禁、四阶段标准输出/错误、两份 EXE、构建结果由 `raw-manifest.json` 逐项绑定。

x64 拒绝路径 `attempt1` 原始驱动退出 1，但失败发生在测试夹具第二次写入 `gate-0.json` 时：`cases=[]`，**探针 EXE 未启动**。旧测试脚本已作为 `raw/rejection-failure/Test-ProbeRejections.before-fix.cjs` 原样保留；新脚本改用独立门禁序号，`node --check` 退出 0，保存在 `raw/next-input/`，但还没有执行。该失败既不是被测程序的拒绝通过，也不是程序故障。后续须在宿主空闲时以新 attempt 名分别验证 x64/x86 拒绝路径，不能覆盖首次原件。

当前正式 Demo 双架构三入口短测已另档，但未打开 `--page=data`。后续真实页面还须核对目标 PID/窗口和自然前台、滚轮后的列表位置、中文输入法组合事件与汉字、任务进度/查询/取消、真实窗口缩放及原生关闭。操作系统输入探针固定输出 `imeCompositionVerified:false`，单靠发送按键不能证明中文 IME 成功；这些项目及 Win7 SP1 x86 当前 Demo 均未在本目录验收。`sourceDeliveryVerified=false`、`benchmarkValid=false`、`releaseReady=false`。
