# 0.3 SDK 中期准入判定

日期：2026-09-24。候选：`0.3.0-preview.5`。本文件只汇总已执行证据；不是正式发布批准。完整门槛见[验证方案](../roadmap/0.3-sdk-validation.md)，逐能力缺口见[能力矩阵](capabilities.md)，原始结果按 run-id 保存在 `verification/production-validation/`。当前 `releaseReady: false`。

| 必须回答的问题 | 当前判定 | 决定性的现有证据与缺口 |
|---|---|---|
| SDK 能否独立交付 | **部分通过，尚未形成完整交付通过** | preview.5 [双架构完整构建、三项 smoke、仓库外生成项目及无 Git 完整源码搬移后重新构建](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)已通过。preview.4 的独立消费者四种模式各三轮与[x64 Release 业务小样](../../verification/production-validation/preview4-upgrade-2026-09-23/README.md)升级回退仍保留原候选身份；新版尚未重跑所有长循环及目标机。底层依赖的搬移后源码重编译、Debug 和目标机交付仍未闭环。 |
| 指定范围能否生产使用 | **未通过** | preview.4 已将修正 HTTPS 校验的内核纳入固定包，[双架构 12 项正负向测试](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)通过；[新固定包生命周期记录](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)完成双架构各 100 次重载及四种模式各 1,000 次，但所有资源门仍未评估，三窗口与 Bridge 分段内存总体上升。GPU 关闭曾出现访问冲突，历史双架构 GPU 重建 30 轮仍有 USER 对象逐轮增长且 Runtime 关闭后残留。[GPU 专项](gpu-lifecycle.md)未达到资源门槛。preview.5 的[任务表容量硬边界](../../verification/production-validation/preview5-task-capacity-2026-09-24/README.md)已有双架构测试，但公共任务业务链还未完整验收。[Win7 SP1 x86 QEMU 四模式短测](../../verification/production-validation/win7-sp1-x86-qemu-2026-09-24/README.md)与[模板/Demo 三项短测](../../verification/production-validation/win7-sp1-x86-qemu-apps-2026-09-24/README.md)仅对应 preview.4；[preview.5 的 Win7 SP1 x86 QEMU 三应用入口短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)已通过，但新版四模式公共消费者与任务满载业务还未执行。8 小时混合负载、物理 Win7 x86 设备与 DPI/IME、完整业务任务、实际信任根/代理、授权和旧 TLS 组件维护仍缺证据。 |
| 相对 Electron 的采用价值是否成立 | **未测，不能判定** | [共享页面与双端最小宿主](../../benchmarks/README.md)已可构建，[三份结果落盘探针](../../verification/production-validation/benchmark-host-smoke-2026-09-23/README.md)确认写入链路；Electron 精确版本已锁定。尚未取得正式两端配对运行、输入到呈现、帧间隔、进程树内存、完整包体积和维护工时原始样本。不能把较小的单个 DLL 或主观流畅性当作产品价值结论。 |

下一阶段按风险顺序收敛：先定位 GPU USER 残留并完成当前公共连接器的双架构生命周期/资源门槛；并行补齐公共任务、桌面模块与六组 Demo 场景；随后冻结共享负载与 Electron 宿主，完成同机性能/体积对照；最后跑 8 小时、目标设备与安装升级回退、安全授权和发布责任审核。每阶段保留失败样本，改变源码或内核后重跑受影响项。正式报告仍分别给出工程交付、指定范围生产和采用价值三个结论。
