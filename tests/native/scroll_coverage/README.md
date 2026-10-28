# Scroll coverage regression

This standalone target compiles the production `ScrollTreeSnapshot.cpp` and
`ScrollController.cpp`. It requires the MiniBlink source tree and does not link
or launch the SDK host, browser or product. Its static CRT keeps the test executable
self-contained when copied into an isolated guest.

From the SDK source root, compile for x86 with the supported Windows SDK:

```powershell
cmake -S tests/native/scroll_coverage -B out/scroll-coverage-x86 -G "Visual Studio 17 2022" -A "Win32,version=10.0.19041.0"
cmake --build out/scroll-coverage-x86 --config Release --target soui_scroll_coverage_tests
```

For x64 use a different build directory and `-A "x64,version=10.0.19041.0"`.
In an environment authorized to execute tests, run the generated
`Release/soui_scroll_coverage_tests.exe`, or use CTest:

```powershell
ctest --test-dir out/scroll-coverage-x86 -C Release --output-on-failure
```

The thirteen cases cover prepared coverage behind the last raster action, no-fence
rejection, no-raster/empty frames, missing or dirty coverage, other fallback
reasons and scene metadata, retired node generations/epochs, and continued
same-epoch smooth motion. Motion cases also exercise horizontal/vertical fractional
samples in both directions, input reversal, actual coverage boundaries, and a
same-epoch scene whose geometry narrows during motion.
The readiness boolean models `TileGrid::hasReadyCoverage`;
these tests do not execute TileGrid raster workers or `LayerTreeHost::recordDraw`.
Actual kernel compilation and isolated GUI wheel/scrollbar validation remain
necessary. This directory is a standalone source-only test entry, so it does not
alter the SDK's default vendor-test inventory or portable SDK package requirements.

## Producer ordering behind the resource fence

`LayerTreeHost::recordDraw` first computes geometry and stores each node's exact
raster rectangle and offset window. This pass still calls
`TileGrid::setRasterCoverageWindow`. `recordDrawChildren` then lets TileGrid consume
that request, clear tile dirty rectangles and allocate logical bitmap entries.
Those entries mean raster work has been prepared; they do not prove its pixels
have been committed. A second coverage query uses the saved rectangle after
`endPostRasterTask`, without rebuilding the scene or issuing another raster request.

The new refresh changes coverage bounds, `coverageComplete` and the incomplete
coverage reason only. It preserves scene version, node generation, scroll epoch,
acknowledgement, geometry, hit order, scrollbar data and unrelated fallback reasons.
If previously incomplete coverage becomes prepared, promotion requires the existing
scene resource fence. Missing or dirty tiles remain ineligible. Existing complete
coverage and empty/no-raster scenes need no additional fence.

The actual producer chain is:

1. `TileGrid::applyDirtyRectsToRaster` clears dirty state and marks the logical
   bitmap before `RasterTaskGroup::postRasterTask` allocates the blend action ID.
2. Each `RasterTask::run` finishes raster work before releasing its group reference.
   The final `RasterTaskGroup::unref` appends all draw-property and blend actions.
3. `ActionsFrame::checkFull` requires a sealed end ID and the complete action count.
   `ActionsFrameGroup::applyActions` applies the actions before calling
   `LayerTreeHost::onActionFrameApplied` with the frame's last action ID.
4. `SceneCoordinator::markResourcesReadyThroughAction` readies the pending scene
   only after that ID reaches its last required action. Normal main-frame
   `AutoRecordActions::leave` calls `recordDraw` (including scene submission) before
   `endRecordActions` seals the frame, so it cannot complete that frame before the
   pending scene has been submitted.

The existing `noRasterCommit || !hasActions` readiness rule and action ID range are
unchanged. The patch does not alter prefetch dimensions, damping constants, pending-scene
policy, raster allocation failure handling, or WebKit's main-thread scroll epochs.

## Fractional motion regression

The first isolated guest execution passed seven coverage cases, but the smooth
motion case failed before its second scene or coverage refresh. The production
controller compared cumulative consumed motion to the desired position with exact
floating-point inequality. With a nonzero base, the add/subtract reconciliation can
introduce roundoff, which then resets the animation's target and velocity. For
example, the fixture's first desired step is approximately `9.802247435626398`;
`(300 + desired) - 300` is approximately `9.802247435626384`, a difference of about
`1.42e-14`.

Both axes now use the existing movement epsilon (`1e-6`) to distinguish a material
restriction from reconciliation roundoff. This keeps an in-flight target while
retaining resets for meaningful displacement differences. Tests retain the original
motion-continuation assertions and add boundary cases; they do not waive the failed
assertion. The test executable prints all case results and returns nonzero if any
case fails.

For a baseline comparison, preserve the original production `ScrollController.cpp`
and its header before editing. Configure a separate build directory with the same
tests and `-DSOUI_SCROLL_CONTROLLER_SOURCE=<absolute-preserved-cpp-path>`. The header
must remain identical for this comparison. Record the source and executable hashes,
and keep baseline outputs separate from patched outputs. This option changes only
which production controller source the standalone regression compiles.

## Validation status

Both x86 and x64 Release test executables compiled with MSVC 19.44 and Windows SDK
10.0.19041.0 on 2026-10-23. The first guest run of the eight-case version passed seven
cases and failed the smooth-motion case on both architectures; its harness did not
capture a usable exit code. That run is retained as failed evidence.

The expanded thirteen-case executables were subsequently executed in an isolated
Windows guest on 2026-10-23. All four launches and exits were captured, and the
seventy extracted event records were checked against the retained raw serial log:

| Controller source | Architecture | Passed | Failed | Process exit code |
|---|---|---:|---:|---:|
| Corrected | x86 | 13 | 0 | 0 |
| Corrected | x64 | 13 | 0 | 0 |
| Preserved baseline | x86 | 7 | 6 | 1 |
| Preserved baseline | x64 | 7 | 6 | 1 |

Both builds use the same thirteen tests and the same refreshed coverage helper;
only the production `ScrollController.cpp` source is switched for this comparison.
The baseline's six failures are all motion cases. Before any second-scene coverage
refresh, both versions produced the same valid first sample, visual position
`309.80224743562638` and consumed displacement `9.8022474356263842`; the baseline
reported `animation_active=0`, while the corrected controller reported `1` on both
architectures. The suite's aggregate exit code was `1` because it includes the
deliberately retained failing baseline; each corrected executable exited `0`.

The [sanitized result summary](../../../docs/verification/scroll-coverage-20261023.json)
records source/executable hashes and counts without application or machine data.
The [technical note](../../../docs/maintenance/scroll-coverage-20261023.md) describes
the patch and its validation limits. No test binary was executed on the build host.

These results exercise production snapshot/controller logic with a controlled tile
readiness input. They do not execute `TileGrid`, raster workers,
`LayerTreeHost::recordDraw`, or a GUI. The full patched kernel build and actual GUI
validation remain separate requirements. GUI comparison should check coverage
reason 128 recurrence and visible scrolling/return/close behavior, while reporting
pending-scene or coverage-boundary fallback separately. No latency or smoothness
improvement is claimed from the unit results alone.
