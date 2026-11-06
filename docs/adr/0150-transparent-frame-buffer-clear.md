# ADR-0150：透明备用画布避免重复清屏

日期：2026-10-31。状态：源码和两架构受影响方法回归通过；匹配内核及真实性能待验。

## 背景

`LayerTreeHost::prepareWritableFrameBufferLocked()` 新建槽时调用 `clearCanvas`，随后构造的 FrameBuffer 只保存字段。在透明槽切换且 previous 可复制时，同次调用又对同 canvas、同 client rect 清屏，然后复制 previous。两次 clear 之间没有绘制或发布，第二次清屏可消除。

这只发生在备用槽首次分配或因尺寸、generation、alpha失效重建时，不能据此承诺持续FPS提升。复用已有槽时可能含旧像素，仍必须清屏。切槽完整 copy 用于保留 dirty 外有效像素，不能删除。

## 决定与兼容

复用既有局部 `valid` 判定：切槽复制前，仅透明且原槽有效时执行 clear；新建槽沿用分配后的第一次 clear。不增加长期状态、额外缓冲、依赖或虚拟方法。正常/异常所有权、两槽/256MiB上限、reader lease、generation、dirty、fence、copy与计数均保持。

备选为增加 createdThisTime 标记，但与已存在 valid 信息重复；删除所有切槽 clear 会使旧透明槽的 source-over 保留残影，拒绝。跳过完整 copy 缺少内容未变证明，拒绝。

## 验证及边界

先从实际生产方法完整字节提取建立回归，原实现应在新槽清屏次数断言失败；候选验证新槽一次clear、有效旧槽仍clear、透明 source-over 的期望像素、opaque、两槽均leased、generation/尺寸/alpha和分配失败。实际 FrameBuffer 构造和策略原字节也要绑定，假Skia依赖明确。它不能代替真实Skia/GDI像素、全内核编译或可见时延/FPS认证。

受影响源码保留原编码和行尾，记录独立raw-byte补丁与前后SHA。已有07def067b新内核与正式模板包结果继续绑定原提交，不重贴给本候选；本批源码收齐后再决定匹配内核/消费者构建，避免每个小改动重复长构建。

回退这一局部条件及其补丁，不动已完成绝对截止、provider、lease或主线资源屏障。完整性能目标与真实性能验收仍未完成。

## 实际结果

x64原实现实际编译后，在`new-transparent-clear-count expected=1 observed=2`指定断言失败，自然exit1/1并闭合；没有把配置失败或abort当作RED。候选x86/x64各6组方法回归通过，编译分别24.497/18.531秒、CTest自然0/0，owned组闭合，6个选定输入执行前后不变。fake Skia模型覆盖半透明/全透明像素、opaque新旧槽、两槽lease、失效重建和分配失败，不替代真实Skia/GDI或完整生产TU。

独立只读审查未见阻断P1/P2。生产只在既有条件增加`&& valid`，原始源码144359→144368B，范围外字节完全相同；0031补丁反向非写入检查通过。9B源码差额不代表DLL/运行目录/ZIP大小。旧c883内核与正式包仍绑定原结果，本候选没有借用其运行或帧率结论。
