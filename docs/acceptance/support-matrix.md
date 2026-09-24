# 候选支持范围与设备矩阵

日期：2026-09-24。状态：采集中，不是正式支持声明。首批真实项目要求最低 Windows 7 SP1，包含 x86；不能仅凭本机 Windows 11 上的 x86 进程结果放行。

## 已使用的设备

当前实际设备为 Windows 11 Pro 25H2，`10.0.26200.9457`，AMD Ryzen 9 9950X（16 核/32 逻辑处理器）。系统同时枚举 AMD Radeon、Intel Arc A380 及虚拟显示适配器；实际呈现所用设备、显示输出和 DPI 仍须逐个测试确认。不能从枚举到显卡或装有中文输入法直接推断功能已经通过。

[设备观察记录](../../verification/production-validation/environment-windows11-26200.json)包含系统、补丁号、CPU、显卡及驱动。性能采样还须登记电源计划、实际显示刷新率、当前 DPI、远程会话状态和活动 GPU；当前窗口生命周期结果不提供这些指标的通过结论。

## 本轮状态

| 组合或能力 | 当前证据 | 状态与缺口 |
|---|---|---|
| 本机 x64 Release | preview.4 固定包公共消费者软件路径重建/重载/三窗口/Bridge 各 1,000 轮、另有静置 1 秒的重载 100 轮；模板及 Demo 默认/React 回归；历史 GPU 重建 30 轮暴露 USER 残留 | [新固定包生命周期](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)、[GPU 专项](gpu-lifecycle.md)和[当前应用回归](../../verification/production-validation/preview4-fixed-2026-09-23/README.md)分别绑定身份；资源总体与业务长稳未通过 |
| 本机 x86 Release | preview.4 固定包公共消费者软件路径重载/销毁重建/三窗口/Bridge 各 1,000 轮、另有静置 1 秒的重载 100 轮；模板及 Demo 默认/React 回归；历史 GPU 重建 30 轮暴露 USER 残留 | [新固定包生命周期](../../verification/production-validation/preview4-lifecycle-2026-09-23/README.md)与[GPU 专项](gpu-lifecycle.md)分别记录；资源门和业务长稳未通过，也不是独立 32 位系统的兼容结论 |
| 本机 x86/x64 Debug | 无 preview.4 当前源码新报告 | 待验证 |
| Windows 10 指定版本/补丁 | 无本轮目标机报告 | 待登记设备和执行；不得推广本机结果 |
| Windows 7 SP1 x86 | [preview.5 QEMU 应用短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-apps-2026-09-24/README.md)：6.1.7601/32-bit、模板与 Workbench 默认/React 三项退出码 0；[同版公共消费者短测](../../verification/production-validation/win7-sp1-x86-qemu-preview5-consumer-2026-09-24/README.md)：Bridge、重载、重建、三窗口各 3 轮；[原生任务容量测试](../../verification/production-validation/win7-sp1-x86-qemu-preview5-task-capacity-2026-09-24/README.md)：1,024 上限、取消回收、并发准入和扫描拒绝退出码 0，串口身份复核 | 新版虚拟机软件路径三应用、四模式短操作及原生容量单测通过；公共任务满载业务链、物理设备、资源趋势、完整 Demo、安装签名、TLS 信任根/代理、中文 IME、DPI 与升级回退仍未通过或未验证，不能生产放行 |
| Windows 7 SP1 x64 | 只锁定了最低系统与 x86 必需组合；x64 是否为首批必需尚待产品矩阵确定 | 若列入支持，须另行完成目标机验收；不能沿用 x86 或 Windows 11 结果 |
| 其他 Windows 11 版本/补丁 | 无本轮目标机报告 | 待登记设备和执行 |
| 100/125/150/200% DPI | 未执行控件、坐标和输入定位验收 | 未验证 |
| 混合 DPI 多屏 | 系统枚举有两种活动显卡输出；未做窗口移动/缩放/断连 | 未验证 |
| 中文 IME | 配置观察不等于输入正确 | 组合输入、候选框定位、光标、撤销及重载均待验证 |
| 键盘和辅助功能 | 尚无本轮数据 | 焦点顺序、快捷键冲突、屏幕阅读器语义待验证 |
| 安装、签名及系统策略 | 尚无目标产品候选 | 需真实签名产物、受限账户、升级/回退后运行记录 |

新增设备报告应包含设备标签（避免个人主机名）、精确系统及驱动、源码与载荷身份、架构/配置、场景及原始记录。不支持或未执行的组合保留原状态；表格填满不能替代测试通过。

完整目标及尚未满足的设备需求保留在[执行记录](progress.md)，最终只按实际通过的组合生成支持声明。
