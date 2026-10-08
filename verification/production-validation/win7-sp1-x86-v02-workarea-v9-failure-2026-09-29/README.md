# Win7 SP1 x86 V02 V9 窗口放置探索：窗口未检出

本目录保存 69 份原始记录和逐字节清单，绑定官方 x86 Release buildId b6caec6c-117e-49eb-86fa-4182e64d5b55、源码 SHA-256 60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640 与冻结输入 SHA-256 98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80。
宿主启动前无其他桌面/虚拟机负载，运行中门禁未记录外部负载；QEMU 原生退出 0，冻结输入及 Win7 基础盘前后校验一致。客体确认为 Windows 7 SP1 x86、PowerShell 2，显示模式从 800×600 变为 1024×768，载荷前置校验通过。
Workbench 进程启动并记录 `window-constructed`、`host-started`，但九次可见窗口探测均返回 `visible unowned top-level target window missing`。原始 `close-attempt.txt` 在约 90 秒记录非零 `MainWindowHandle=196766` 和 `CloseMainWindowPosted=True`；50–90 秒之间没有同一探针采样，不能据此认定首次可见时间。客体由测试脚本请求关闭，Workbench 进程原生退出 0；这不是自然关闭或可见页面通过。客体整轮 `run-exitcode.txt` 为 1，只有 29/116 预期串口文件；严格成功校验原始退出 1。命令只发送一次，截图和所有失败原样本保留。
不能据此确认工作区定位修正解决了 Win7 任务栏遮挡，也不能确认真实滚动、中文输入法组合或公共任务并行。需在新的冻结输入和尝试中查明窗口可见性；本次输入和结果不得覆写。`releaseReady` 继续为 false。
