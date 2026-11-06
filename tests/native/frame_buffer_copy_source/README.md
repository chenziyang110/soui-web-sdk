# 完整 FrameBuffer 准备方法回归

此入口逐字节投影实际 `LayerTreeHost.cpp` 的完整
`prepareWritableFrameBufferLocked()`、`FrameBuffer` 构造/析构和
`clearCanvas()`，并使用完整实际 `FrameBufferLeasePolicy.h`。配置时生成的
`generated/source-binding.json` 记录完整源文件 SHA-256、片段 SHA-256、
字节范围和唯一边界。投影直接写入原始字节，并拒绝非严格 UTF-8 片段；
不重写生产条件、删除 copy 或用独立策略函数替代实际方法。

依赖 fake 限于 Skia canvas/bitmap/paint、平台分配和 copy trace counter。
fake bitmap 保存四字节预乘 alpha 像素，`drawBitmap` 执行整数 SrcOver；
透明源不会覆盖旧目标，因此复用槽少 clear 会留下可观察的像素错误。
测试同时观察 clear/draw 次数与字节数、平台分配、临时 canvas 释放、
真实 shared_ptr 槽位 lease 和 FrameBuffer generation/bytes。
它不证明真实 Skia、GDI、完整 Host 生命周期或性能资格，不启动窗口或输入。

从仓库根目录独立配置、构建及执行（以 x64 为例）：

```powershell
cmake -S tests/native/frame_buffer_copy_source -B out/frame-buffer-copy-source-x64 -A x64
cmake --build out/frame-buffer-copy-source-x64 --config Release --target frame_buffer_copy_contract
ctest --test-dir out/frame-buffer-copy-source-x64 -C Release -R '^kernel\.native\.frame-buffer-copy-source$' --output-on-failure
```

单独执行原缺陷 RED 场景（新建透明备用槽应只 clear 一次）：

```powershell
out/frame-buffer-copy-source-x64/Release/frame_buffer_copy_contract.exe --case transparent-new
```

旧源码会观察到两次 clear；完整像素复制仍必须执行且计数为一次。
其余独立 `--case` 为 `transparent-reuse`、`opaque`、`leases-exhausted`、
`size-alpha-generation` 和 `allocation-failure`。全量入口不倒置失败状态。
`-DSOUI_DISTRIBUTION_ROOT=<source-root>` 可选择另一份实际源码作对照；
每次配置都会重新生成其字节绑定。
