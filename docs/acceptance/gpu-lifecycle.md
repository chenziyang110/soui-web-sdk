# GPU 生命周期专项记录

日期：2026-09-23。此记录针对本机显卡/驱动、独立公共 SDK 消费者的 `recreate` 模式；每轮创建页面、完成标准连接器探针、销毁 WebView，3 次预热后测量 30 轮，每轮静置 100 ms。主表为 x64 Release，随后补充 x86 和软件路径对照。原始构建身份、载荷散列、逐轮事件与资源样本保存在 [专项归档](../../verification/production-validation/gpu-lifecycle-2026-09-23/)。所有候选的 `releaseReady` 均为 false。

| 内核 | cycle 3 → 33 Private Bytes | 普通句柄 | USER 对象 | 运行结果 |
|---|---:|---:|---:|---|
| [原固定 DLL](../../verification/production-validation/gpu-lifecycle-2026-09-23/prebuilt/result.json) `8f155e84` | 148,824,064 → 309,854,208 | 419 → 659 | 16 → 76 | 30 轮正常退出，资源持续增长 |
| [三项资源修补](../../verification/production-validation/gpu-lifecycle-2026-09-23/three-fixes/result.json) `2ed34e54` | 147,091,456 → 310,341,632 | 416 → 626 | 13 → 43 | 30 轮正常退出，资源持续增长 |
| [合成线程释放 GPU 表面](../../verification/production-validation/gpu-lifecycle-2026-09-23/thread-affinity/result.json) `1ddad468` | 143,638,528 → 241,467,392 | 416 → 638 | 13 → 46 | 30 轮正常退出，资源持续增长 |
| [再释放 Skia 接口引用](../../verification/production-validation/gpu-lifecycle-2026-09-23/interface-release/result.json) `02bd76ee` | 142,254,080 → 221,794,304 | 418 → 624 | 13 → 43 | 30 轮正常退出，资源持续增长 |

每个结果分别绑定候选 DLL 和消费者载荷，不把跨候选的数值变化当成严格因果测量。按同一预热末点到末点口径，最后候选的 Private Bytes 仍增加 79,540,224 字节，普通句柄增加 206，USER 对象增加 30。逐轮记录表明增长持续，因此不能由正常退出或较低的末点宣布 GPU 生命周期合格。监督器的 `resourceGate` 仍为 `not-evaluated`；本专项记录依据原始样本指出资源验收失败，尚未替机器报告伪造通过值。

同一最终内核的 [x86 GPU 30 轮](../../verification/production-validation/gpu-lifecycle-2026-09-23/interface-release-x86/result.json)也正常退出，但 Private Bytes 从 119,545,856 增至 173,711,360、普通句柄从 417 增至 609、USER 从 13 增至 43。相同最终 x64 内核和消费者的[软件路径 30 轮对照](../../verification/production-validation/gpu-lifecycle-2026-09-23/interface-release-cpu-x64/result.json)普通句柄也从 305 增至 485、Private Bytes 从 35,852,288 增至 107,343,872，说明 30 轮的句柄与进程内内存增长不能全归因于 GPU。随后[软件路径 100 轮](../../verification/production-validation/gpu-lifecycle-2026-09-23/cpu-x64-100-recreate/result.json)证明这些量并非持续线性增加：普通句柄在 cycle 33 为 483、cycle 53 回落至 329、cycle 93 为 290；Private Bytes 也经历回收，Runtime 关闭后降至 14,016,512。短样本端点不能替代长期趋势。软件路径 USER 保持 9；GPU 的 USER 增长在两架构均为每轮约 1 个，x64 Runtime 关闭后仍有 37 个 USER 对象和约 137 MiB Private Bytes，须单独追查 GPU 专属对象。

[关闭诊断](../../verification/production-validation/gpu-lifecycle-2026-09-23/gpu-shutdown-interface-release.txt)显示 33 个真正创建的 WGL 上下文在合成线程上完成 `wglMakeCurrent`、解绑、`wglDeleteContext` 和 `ReleaseDC`，返回值均成功。进程窗口类别采样在 cycle 3 与 33 都只看到一个 `IME` 和一个 `MiniBlinkTimerWindowClass`；补查消息窗口的 [5 轮运行](../../verification/production-validation/gpu-lifecycle-2026-09-23/message-window-probe-x64/result.json)每轮固定另有两个 `OleMainThreadWndClass`，USER 计数却继续逐轮加 1。基于[独立 WGL 探针源码](../../tests/diagnostics/wgl-context-lifecycle.cpp)的 [33 轮原始输出](../../verification/production-validation/gpu-lifecycle-2026-09-23/wgl-probe-x64-run.log)在同机反复创建、呈现、删除上下文并设置交换间隔，USER 与句柄保持稳定。故障边界收敛到完整 Skia/内核/宿主路径，不能仅归因于基础 WGL 创建删除；这些诊断仍不能确定剩余对象的准确类型。

旧版默认 GPU 的 [关闭访问冲突](../../verification/production-validation/regression-3dcd7b97ea5f/x64-gpu-default-failure/smoke-Release.json)仍是真实失败记录。当前显式 GPU 的 Workbench 20 次进程启动/退出及公共消费者短测未复现该退出码，但测试入口和候选输入不同，不能据此关闭崩溃缺陷。生产模板继续默认软件呈现。下一步需要隔离 WGL/驱动自身的 USER 与句柄行为，定位剩余对象所有权，再以双架构、1,000 次生命周期、100 次重载和 8 小时混合负载重新验收 GPU 路径。
