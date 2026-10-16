# 0103 draft: continuous integer refresh-rate policy

2026-10-15. Written before prototype implementation; Source19 remains immutable while its acceptance chain runs.

The pacer accepts only 60/120/144 Hz, and Workbench rounds a 100 Hz monitor down to 60 Hz. This contradicts the user's requirement for high refresh on available hardware. The maintainer now asks to ensure high-refresh support without waiting for a particular display. No new physical display or performance result is implied.

Accept integer pacing targets from 30 through 1000 Hz, matching the existing display-query bounds. Clamp a requested target to the reported display rate directly, including 75/100/165/240 Hz, rather than discrete tiers. Unknown/out-of-range display rates retain the conservative 60 Hz ceiling. Invalid requested targets remain errors. Fractional display timings remain rounded by the existing Windows mode query; accurate rational physical presentation still belongs to the display evidence collector.

Use one shared platform-neutral rate policy for the pacer, Workbench control boundary and clamping. The environment override is diagnostic only and must be an entire unsigned decimal integer in range. Moving the window or receiving a display-setting change invalidates its mode cache. Preserve coalescing, missed-frame skipping, balanced timer resolution, stop/restart ownership and Win7 timer fallback. Requested rate, active pacing rate and physical presentation remain different quantities.

The Demo control accepts the full integer range and offers common 60/75/100/120/144/165/240 choices. This extends accepted values without changing existing valid requests. Update API/testing/changelog and the roadmap's measurement scope: current hardware configuration is recorded honestly; physical rates not available remain unmeasured and do not block implementing support. Do not claim timing quality from message counts or injected display overrides.

Alternatives: adding only 100 preserves the same defect for other displays; forcing a high timer rate on a 60 Hz display wastes work and is not physical high refresh. A new presentation-clock architecture is outside this correction.

Verification: reproduce the original 100 Hz rejection, then test boundary rejection, monitor clamping, strict override parsing, real timer pulses at common high rates, coalescing after a blocked UI, stop/restart and both timer paths on x86/x64. Exercise actual authenticated Demo control with valid and invalid JSON values, rebuild affected native/Web targets, and retain separate physical-presentation evidence.
