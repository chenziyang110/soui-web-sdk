# 输入路由器回归 — 2026-10-23

范围：x86 Release SDK 路由器对过期滚动屏障的契约回归。

原实现与修复候选的十二项差分用例分别为 7/12 和 12/12 通过。随后维护测试目标链接生产 `Soui::Sdk`，在隔离 Windows 来宾运行，十二项全部通过、退出码 0；测试 EXE 的 SHA-256 为 `D09F7E0E1C545C04DD965E04E9738DFDFCB0176F3353500C4C4B37845E913DD6`。

用例覆盖滚轮或前次有效屏障之后的新捕获失败、失败时部分写入输出、成功屏障字段完整保留、缓存不被污染、抬起 FIFO 顺序、调用方屏障、过期代际及后续成功恢复。结果只证明路由器契约，不代表滚动流畅性、完整 GUI、x64 或发布资格验收。

隔离来宾原始串口记录保存在仓库外；以下摘录对应的实际测试结果：

```text
JZMAINTENANCE PASS wheel_then_false_untouched
JZMAINTENANCE PASS wheel_then_false_partial
JZMAINTENANCE PASS prior_success_then_no_target
JZMAINTENANCE PASS prior_success_then_partial_failure
JZMAINTENANCE PASS no_history_partial_failure
JZMAINTENANCE PASS fresh_success_all_fields_preserved
JZMAINTENANCE PASS fresh_success_replaces_all_old_fields
JZMAINTENANCE PASS failure_does_not_poison_cached_sample
JZMAINTENANCE PASS up_submission_order_without_recapture
JZMAINTENANCE PASS caller_barrier_cleared_on_failed_down
JZMAINTENANCE PASS stale_generation_has_no_side_effect
JZMAINTENANCE PASS later_success_survives_failed_partial_capture
JZMAINTENANCE interaction_router_tests summary count=12 passed=12 failed=0
exitCode=0
```
