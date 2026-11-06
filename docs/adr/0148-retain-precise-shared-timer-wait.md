# ADR-0148：保留 SharedTimer 高精度截止唤醒

日期：2026-10-30。状态：proposed；限定回归与 x64 合成场景复测通过，完整发布资格未验证。

## 背景

PR129 冻结候选在隐藏 Windows 11 desktop 的相同计算/DOM/Bridge 场景中，16ms 循环吞吐重复低于基线约 31%。外部拆分短测中，setTimeout 实际等待均值从 17.46ms 增至 24.68ms，Bridge 往返从 0.94ms 增至 1.53ms。基线使用带 HIGH_RESOLUTION 标志的 waitable timer；候选用普通 threadpool timer 与 timeBeginPeriod 补偿。后者不保证不可见窗口进程的精度。

## 决定

- future deadline 优先复用高精度 waitable timer 和一个持有的 threadpool wait。动态查询 CreateWaitableTimerExW，使用 HIGH_RESOLUTION 标志；旧系统不支持时保留原 threadpool timer，再失败保留 ticketed native timer。
- 不恢复每次 deadline 的注册/同步注销。重设或取消只 disarm；callback 仍只发布静态窗口/ticket，绝不执行 Blink 或持有 task/owner 指针。
- 在 owner 上复核绝对 WTF deadline，旧 callback 至多导致提前通知与按原目标重设，不能提前执行 future task。
- publication lock 内安排等待/票号；关闭先禁止发布、取消等待，锁外 drain/close threadpool wait，再关闭 waitable handle。两个原 pool timer 的关闭顺序保持。无 idle pulse 或永久精度提升。
- waitable 创建、wait 创建或 arm 失败时走原兼容路径，短期 timeBeginPeriod 只用于该路径。重投递 retry 保留现有 pool timer。

## 兼容、备选与回退

不改变公开 ABI、共享预算默认值、页面、任务工作量或最低 Windows。仅增加 Windows 7 已有的 threadpool wait API；高精度标志使用动态检测。未选择长期 timeBeginPeriod、设置进程电源豁免或恢复逐 deadline UnregisterWaitEx，以免用全局策略或同步 callback 排空掩盖退化。可独立回退本修复，保留既有绝对 deadline 与 provider 预算。

## 验证边界

先用冻结 B/C 拆分短测复现，再检查真实头文件的高精度选择、fallback、提前唤醒、旧票号、复用和锁外关闭契约，最后重编受影响真实 TU、链接冻结其他对象，用原固定负载做短配对。x64/x86 各 3 项 CTest、64 个主契约断言通过。x64 修复后默认预算两轮分别 1718/1709 批次，对照基线 1685/1693，原候选 1163/1162；1000µs 下原候选 1157、修复 1706。所有真实轮次正常退出且末批校验一致，详见[限定验证](../performance/shared-timer-precision-20261030.md)。

最小重链候选不是完整公开发行构建；不扩大到生产帧率、物理输入延迟、长期稳定性、包尺寸或全部 PR 已验收的结论。
