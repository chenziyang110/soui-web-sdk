# ADR-0005：生产模板默认使用稳定呈现路径

- 状态：accepted for preview；GPU compositor 仍为实验能力
- 日期：2026-09-22
- 关联：[生产验证进度](../acceptance/progress.md)、[已知限制](../known-issues.md)

## 背景

模板和 Workbench 原先默认设置 `SOUI_GPU_COMPOSITOR=1`，并在模板中同时设置 `SOUI_GPU_DIRECT_PRESENT=1`。公共独立消费者的默认软件路径能够完成 reload、重建、多窗口和桥接短测，但完整应用 smoke 在 x64 三个入口都于 `wkeDestroyWebView` 阶段以 `0xC0000005` 退出。

对同一个已通过的独立消费者设置 `SOUI_GPU_COMPOSITOR=1` 可稳定复现退出；`SOUI_GPU_DIRECT_PRESENT` 为 `1` 或 `0` 都不能避免。原始失败、退出码和关闭日志已归档。当前证据只把故障边界收敛到 GPU compositor 生命周期，尚未证明更深层的具体内核对象。

## 决定

1. 新项目模板显式清除 GPU compositor 环境变量，默认使用已通过双架构应用关闭回归的软件呈现路径。
2. Workbench 无 renderer 参数时使用软件路径。`--renderer gpu`、`readback` 和 `auto` 继续服务内核专项实验，不计入生产默认能力。
3. 默认 smoke 必须验证 Starter、Workbench 默认页和 React 页的启动、页面/Bridge 就绪、WebView 销毁、窗口销毁及进程正常退出。
4. GPU 能力单独验收，至少包含重复创建/销毁、多窗口、显卡/驱动组合、最小化恢复、8 小时混合负载和真实呈现测量；关闭访问冲突解决前不得成为模板默认值。
5. 软件默认路径的通过不能转述为 GPU 性能结论，也不能宣称已经达到 Electron 的流畅性。

## 影响

新项目获得可重复关闭的保守默认值，Workbench 仍可显式复现和调试 GPU 后端。默认模式可能低于 GPU 目标的峰值动画性能，因此性能对标必须同时报告软件基线和未来通过验收的 GPU 路径。

## 当前证据

- 失败源码指纹 `3dcd7b97ea5f...`：x64 三个应用入口均在关闭时访问冲突；独立消费者设置 GPU 环境后同样失败。
- 稳定默认源码指纹 `6649c3929622...`：x86/x64 Release 构建、单元/契约测试及每个平台三个应用入口全部通过。
- 两组证据的 `releaseReady` 均为 false；资源、长稳、平台、安全、授权和 Electron 对照门槛仍未关闭。
