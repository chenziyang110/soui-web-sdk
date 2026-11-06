# 私有渲染阶段成本诊断

此目录用于评估默认 CPU 路径的唤醒、raster、复制和原生提交成本。它是独立 benchmark overlay，不是默认 SDK、默认 MiniBlink DLL 或新的公开 API；设计与限制见 [ADR0160](../../docs/adr/0160-private-render-phase-cost-observation.md)。

`source-baseline.json` 固定八份原始 vendor 文件的字节身份。`prepare-overlay.py` 只在仓库外的新目录复制并插入接线，保留可审补丁与原混合换行；不修改默认 vendor 物理树。七个 TU 必须按各架构原工具链、实际最终链接对象和严格 include 闭包构建。原 PCH 不能直接带到新源码，必须显式核对其去除与原 forced include；不能把增量重链称为 fresh whole-kernel。

`RenderPhaseCost.h` 固定 32 个 producer 槽及七组 64 桶。`RenderPhaseCostHooks.h/.inc` 将其绑定到原 trace 注册、stop/quiescent 生命周期。未开启、已停止或普通未注册线程不新增 QPC；已注册但缺映射是故障。最终停止且所有 producer 排空后才能输出完整直方图，busy、故障和缺失不得解释为零。

`DiagnosticPhaseClient.h` 仅用于显式诊断消费者。消费者编译时必须绑定该架构实际诊断 DLL 的 SHA，运行时在加载和发送私有 key 前再次核对文件与模块身份；旧默认 DLL 会在加载前被拒绝，不能盲探未知冷 key。客户端不更换原 scalar trace 协议，最终私有 JSON 独立保存。

`prepare-consumer.py` 从普通性能 Host 的八源原件生成独立九源消费者。只有这个副本允许 owner 队列中的 `diagnostic-close`，普通 Host 不增加该动作。它在原 scalar 完成后保存最终私有快照；失败返回非零。

`prepare-page.py` 在原三文件性能页中加入明确的实验驱动：原 Canvas 240 角色预热约 10 秒，再使用原 Begin/End 流程记录完整 30 秒。原 draw、模拟、工作量、录制和焦点检查保持不变；新增 100ms 页面控制轮询计入本实验的扰动。必须确认两份原生快照完整、可用且已落盘，再确认页面报告落盘，才能请求专属正常关闭。它不生成系统输入，不把私有桌面结果称为前台或物理性能。

从仓库根目录仅准备源副本的例子如下；输出必须不存在且在仓库外。这里的命令不编译、不部署、不运行 GUI。

```powershell
$phaseLab = Join-Path (Split-Path (Get-Location).Path -Parent) 'render-phase-lab'
python benchmarks/render-phase-cost/prepare-overlay.py --kernel-source third_party/sources/miniblink --output (Join-Path $phaseLab 'overlay')
python benchmarks/render-phase-cost/prepare-consumer.py --source-root benchmarks/sdk-performance-host --output (Join-Path $phaseLab 'consumer')
python benchmarks/render-phase-cost/prepare-page.py --source-root benchmarks/sdk-performance-host --output (Join-Path $phaseLab 'page')
```

唤醒迟到使用同一 WTF monotonic 时域的 due 与实际 dispatch，单位微秒。其余五组为 QPC inclusive wall duration，包含抢占和内部等待，不是线程 CPU。它们覆盖整个注册 trace 生命周期，包含启动、预热和关闭；不能与 DOM 30 秒窗口混用，不能将阶段分位数相减、累计重叠范围为 CPU 占比，或推断逐帧因果、GPU 成本、scanout 与输入到屏幕延迟。

当前记录和失败以任务实际构建收据为准。CPU 合同已覆盖两架构；真实诊断内核、冷 ABI、消费者与运行证据必须分别绑定，不能由源码准备、语法检查或一个成功切片推导整轮通过。缺少有效热点与收益证据时，保留默认 GPU、相位源及调度策略。
