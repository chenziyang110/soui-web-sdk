# ADR-0149：原生位图提交返回前完成发起线程的 GDI 批次

日期：2026-10-31。状态：proposed；两架构契约回归与增量内核、x64隔离桌面短测通过，真实桌面CRC及完整公开构建未验；详见[限定验证](../performance/native-bitmap-completion-20261031.md)。

## 背景

CPU HWND 提交持有 FrameBufferLease，调用 BitmapPlatformDevice::DrawToNativeContext，返回后允许释放租约并复用像素。缓存 DC 路径的 EndPlatformPaint 不刷新 GDI，唯一显式刷新位于 onAccessBitmap；后续直接 raster 写入经过 accessPixels，不保证经过该钩子。因此不能依赖一次未来的 bitmap 访问建立本次提交的源图读取完成边界。

微软 GdiFlush 契约要求在交出共享 GDI 对象所有权前完成发起线程批次：https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush 。本次将该边界置于实际原生绘制方法内部，使它不依赖缓存 DC 是否恰好被删除或 mirror 是否发生额外访问。

历史 CRC 坏帧没有原件；此路径在基线中已存在，多轮实际采集与独立 DIB 实验没有重现该错误。本决定修复明确的提交完成契约缺口，不将历史 CRC 归因为已证实的 GDI 竞态。

## 决定

- 保留 BitBlt/AlphaBlend、恢复变换、结束绘制及 DC 生命周期顺序。
- 在源 bitmap/DC 仍有效时，恢复变换后、EndPlatformPaint 和可能的 ReleaseBitmapDC 前，显式调用一次发起线程 GdiFlush。
- 分别记录绘制与刷新结果；绘制失败仍执行刷新及原清理，不用短路表达式跳过刷新。设备方法仅在两者均成功时返回 true。
- 不改线程模型、双槽租约、dirty 合并、mirror 策略、CRC 规则或公开 ABI。上层 void wrapper 仍不传播设备返回值，本次不扩大为 Host 错误协议变更。

## 权衡与回退

每次原生区域提交增加一个刷新边界，可能增加系统调用成本；不承诺吞吐、帧率或输入延迟改善。刷新只作用于发起线程，不使多个区域原子呈现，也不替代其他线程/DC 的同步。选择显式边界而非重新启用全画面 mirror 拷贝或全进程禁用 GDI batching。可独立回退此局部修改，保留严格 CRC 与坏帧取证补丁。

## 最小验证

执行真实方法源码的确定性延迟 GDI 契约回归（旧源码失败、新源码通过），覆盖不透明/Alpha、绘制失败仍刷新、刷新失败及清理顺序；重编受影响实际 TU 并链接保持调度修复的冻结对象；用新内核做一组实际短输入对照。合成批次不等于真实 Windows 故障复现，外部增量内核不等于完整公开构建或发布资格。
