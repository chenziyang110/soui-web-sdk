# ADR-0006：GPU 表面的创建与销毁归属同一合成线程

- 状态：实验性修补，未通过 GPU 生产验收
- 日期：2026-09-23
- 关联：[GPU 生命周期证据](../acceptance/gpu-lifecycle.md)、[稳定默认值](0005-stable-renderer-default.md)

## 背景

`GpuSkiaSurface` 在 `LayerTreeHost` 的合成线程中创建并将 WGL 上下文绑定到该线程。原析构次序先关闭合成线程，随后在 UI 线程删除 GPU 表面；此外，`GrGLCreateNativeInterface()` 返回的初始引用未在表面释放。独立 SDK 消费者的 GPU 重建短测虽然可以正常退出，但 x86/x64 均出现每轮约 1 个 USER 对象残留和明显的进程内资源增长。

## 决定

销毁 `LayerTreeHost` 时，停止并等候待处理合成工作后，将 GPU 表面交给合成线程在自身关闭前析构。`GpuSkiaSurface::reset()` 在释放 `GrContext` 后释放自身持有的 `GrGLInterface` 引用。仅在诊断环境变量 `SOUI_GPU_SHUTDOWN_TRACE` 指向绝对路径时附加写入 WGL 上下文绑定、解绑、删除及 DC 释放返回值。该内核改动保留为独立补丁 `0005-gpu-surface-lifecycle.patch`，固定预编译 DLL 暂不替换。

## 验证与限制

x86/x64 候选均从源码编译，公共消费者 GPU 重建各 30 轮正常退出。x64 在这些非严格配对的短样本中 Private Bytes 末点下降，但 USER 对象仍逐轮增加；x86 亦然。WGL 调用返回成功，同机 33 轮最小 WGL 探针资源稳定，普通和消息窗口计数也无法解释剩余 USER 增长。因此本决定只修正可见的线程归属及引用释放，不宣称已定位或修复全部资源问题。模板与 Workbench 继续默认软件呈现；GPU 路径须通过剩余对象定位、两架构长循环、平台和驱动组合及 8 小时混合负载后再考虑升级为默认。
