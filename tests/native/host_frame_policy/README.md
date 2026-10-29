# Host frame policy SOURCE

No compilation or test execution was performed. This pure policy target has no
W01 recorder, vendor or WebView2 dependency. Root registers target
`soui_host_frame_policy_tests`, CTest `sdk.native.host-frame-policy`.

Matched fractional cadence is the normal controller. A CPU-path explicit
`SOUI_CPU_DRAW_INTERVAL_MS` or legacy compositor-rate experiment selects legacy
mainFrameRate=0, unless the caller explicitly selected the main-frame scheduler
experiment. GPU continues to ignore CPU-only interval overrides. A nonpositive
target cannot claim an active fractional CPU controller. The default legacy
CPU interval remains17ms; zero draw interval is never introduced as a default.

Host samples target/override once per pump. Every main-controller transition
invalidates its draw-interval cache, ensuring a legacy override is actually
sent even when numerically equal to the prior fractional mode's cached value.
Source cases cover the selection matrix and overflow-safe ceiling intervals;
actual source-kernel pacing and performance remain specialist qualification.
