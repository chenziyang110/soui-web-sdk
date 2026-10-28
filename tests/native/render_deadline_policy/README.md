# Legacy 绘制 deadline：专人验证入口

此独立入口直接包含生产 LegacyPaintRetryPolicy.h，用显式失败计数保留 Release 检查。覆盖单 outstanding ticket、提前回调的剩余时间、取消/resize/销毁、旧票号、计数耗尽、非法时间及长间隔。它不执行真实 LayerTreeHost、Win32 投递或绘制，也不证明像素/性能正确。

当前只交付源码和静态审查，**未配置、编译或执行**。验证人员从仓库根运行：

```powershell
cmake -S tests/native/render_deadline_policy -B out/render-deadline-x64 -A x64
cmake --build out/render-deadline-x64 --config Release
ctest --test-dir out/render-deadline-x64 -C Release --output-on-failure
```

x86 使用独立目录并将架构换为 Win32。真实 LayerTreeHost 还需分别验证 legacy 和默认 DIRECT_HWND/independent：提前唤醒不忙转、dirty 合并/投递失败后保留、deadline 后一次绘制、resize 和关闭时旧任务无效。任务投递拒绝后只保留 dirty，由后续 owner 服务/新 dirty 重试；资源通知和有界失败兜底由后续 W06 交付，不能以本 helper 用例代替其验收。
