# 0.3 SDK 中期准入判定

日期：2026-09-23。候选：`0.3.0-preview.4`。本文件只汇总已执行证据；不是正式发布批准。完整门槛见[验证方案](../roadmap/0.3-sdk-validation.md)，逐能力缺口见[能力矩阵](capabilities.md)，原始结果按 run-id 保存在 `verification/production-validation/`。当前 `releaseReady: false`。

| 必须回答的问题 | 当前判定 | 决定性的现有证据与缺口 |
|---|---|---|
| SDK 能否独立交付 | **部分通过，尚未形成完整交付通过** | preview.4 固定内核的 x86/x64 Release SDK、模板、Workbench 构建和三项实际应用 smoke 通过；独立消费者四种模式各三轮通过。[当前包生成的仓库外项目](../../verification/production-validation/preview4-independent-2026-09-23/README.md)完成双架构 Release 编译、真实启动和关闭。[完整源码包搬移实验](../../verification/production-validation/preview4-source-2026-09-23/README.md)也从无 Git 解压目录完成双架构应用构建和启动，但底层依赖的搬移后源码重编译未执行。历史 preview.3 的长循环不能记为新固定包通过；业务小样、升级回退后的重新编译运行与 Debug 仍未闭环。 |
| 指定范围能否生产使用 | **未通过** | preview.4 已将修正 HTTPS 校验的内核纳入固定包，[双架构 12 项正负向测试](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)通过。GPU 关闭曾出现访问冲突，历史双架构 GPU 重建 30 轮仍有 USER 对象逐轮增长且 Runtime 关闭后残留。[GPU 专项](gpu-lifecycle.md)未达到资源门槛。当前固定包的千次循环、8 小时混合负载、目标系统/DPI/IME、多窗口业务任务、实际信任根/代理、授权和旧 TLS 组件维护仍缺证据。 |
| 相对 Electron 的采用价值是否成立 | **未测，不能判定** | 已有确定性万条数据的页面雏形，但无两端锁定版本、同机同页面的完整宿主、输入到呈现、帧间隔、进程树内存、包体积和维护工时原始样本。不能把较小的单个 DLL 或主观流畅性当作产品价值结论。 |

下一阶段按风险顺序收敛：先定位 GPU USER 残留并完成当前公共连接器的双架构生命周期/资源门槛；并行补齐公共任务、桌面模块与六组 Demo 场景；随后冻结共享负载与 Electron 宿主，完成同机性能/体积对照；最后跑 8 小时、目标设备与安装升级回退、安全授权和发布责任审核。每阶段保留失败样本，改变源码或内核后重跑受影响项。正式报告仍分别给出工程交付、指定范围生产和采用价值三个结论。
