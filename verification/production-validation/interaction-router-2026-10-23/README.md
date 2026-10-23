# Interaction router regression — 2026-10-23

Scope: x86 Release SDK router contract tests for stale scroll-barrier handling.

The original implementation failed 5 of 12 cases; the candidate passed all 12. The exact test binary was run in an isolated Windows guest, returned exit code 0, and had SHA-256 `D09F7E0E1C545C04DD965E04E9738DFDFCB0176F3353500C4C4B37845E913DD6`.

The cases cover pointer-down after wheel input or prior valid barriers when a new capture fails, partial failed output, successful barrier field preservation, cache integrity, pointer-up FIFO ordering, caller-supplied barriers, stale generations, and later recovery. This confirms the router contract only. It does not establish smooth scrolling performance, complete GUI behavior, x64 behavior, or release readiness.

The isolated guest serial record is maintained outside this repository; its relevant result is reproduced below:

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
