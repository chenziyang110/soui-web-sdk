# Host visibility / Commit 生产源码回归

目标为 `soui_host_visibility_source_tests`，CTest 名为
`kernel.native.host-visibility-source`。C++17，使用生成 include 目录和
MSVC `/utf-8`；这些实际生产片段是 UTF-8 原字节，不能按 CP936 解码。
generator 在配置阶段运行，不单独链接 vendor `.cpp`。

generator 按唯一 ASCII 边界提取实际 visibility policy/state/task、
`CommitTask`、`WebPageImpl::scheduleCommit()`、main-frame、
`AutoRecordActions` 和 `LayerTreeHost::setVisible()`。
`schedule_commit.inc` 独立绑定 `setNeedsCommitAndNotLayout()` 与
`scheduleCommit()`；`main_frame.inc` 从 `setNeedsCommit()` 开始。
`source-binding.json` 的 11 项保留完整源/片段 SHA256 和原字节范围，供
source/HEX 守卫核验。生成器直接保存原始字节，没有替换生产调度算法。

保留的五项 visibility 场景覆盖隐藏时的 dirty/resource 保留、旧延迟 Commit
之前的 fresh immediate restore、same-true restore、投递/分配拒绝、资源恢复、
layout 中隐藏、activity 延迟关闭，以及关闭后的 stale receipt。
新增八项 Commit 场景检查小数 frame target、不同 producer sample 下的相同目标、
minimum delay、34 ms frame cap、更长 minimum delay、禁用 pacing、过期目标、
合并、取消/拒绝析构计数，以及真实 `AutoRecordActions` 中提前 flush
layout/paint 与按时 `beginFrame` 的区别。程序报告固定 13 个场景，断言在
`NDEBUG` 下有效，失败返回非零。

夹具控制 WTF 时钟，投递接收端记录实际收到的任务类型、priority、post sample
与 due。relative 入口在接收端按整数毫秒重建 due；absolute 入口记录原始 double。
`postClockAdvance` 模拟 producer 到投递取样之间的间隙。所有入口都转移任务
所有权并保持异步，显式 run/cancel 才执行/销毁接纳的任务。
例如 producer `100.125`、frame target `100.13225`、post 间隙 `0.0004` 时，
旧 relative 实现接收 `8 ms` 并得到 `100.1334`，断言要求原目标 `100.13225`。
此场景应在未修改生产投递路径的基线中编译并 RED；必须先记录实际基线结果，
再验证候选。

Blink layout/record/resource 和 scheduler receiver 是明确的测试依赖。
该入口执行实际 `CommitTask`、`scheduleCommit` 与 beginFrame gate 片段，
不执行真实 WebThread/TaskPair/admission/timer 链，也不创建窗口、执行 raster
worker 或证明 native pixels/FPS 改善；完整链路由独立 worker fixture 验证。
父任务已用 LLVM22/MSVC142 在 x86/x64 实际运行 generator、编译并执行：原路径各7/13场景通过、指定fractional断言失败；候选各13/13场景、96断言通过。构建/候选执行均自然0/0且owned group闭合。详见ADR-0146与对应source record；这仍不验证完整内核或FPS。
